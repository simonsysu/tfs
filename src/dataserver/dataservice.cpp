/*
 * (C) 2007-2010 Alibaba Group Holding Limited.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 *
 * Version: $Id: dataservice.cpp 1000 2011-11-03 02:40:09Z mingyan.zc@taobao.com $
 *
 * Authors:
 *   duolong <duolong@taobao.com>
 *      - initial release
 *   qushan<qushan@taobao.com>
 *      - modify 2009-03-27
 *
 */
#include "dataservice.h"

#include <Memory.hpp>
#include "common/new_client.h"
#include "common/client_manager.h"
#include "common/func.h"
#include "common/directory_op.h"
#include "new_client/fsname.h"

namespace tfs
{
  namespace dataserver
  {
    using namespace common;
    using namespace client;
    using namespace message;
    using namespace std;

    static const int32_t SEND_BLOCK_TO_NS_PARAMETER_ERROR = -1;
    static const int32_t SEND_BLOCK_TO_NS_NS_FLAG_NOT_SET = -2;
    static const int32_t SEND_BLOCK_TO_NS_CREATE_NETWORK_CLIENT_ERROR = -3;
    static const int32_t SEND_BLOCK_TO_NS_NETWORK_ERROR = -4;

    static int send_msg_to_server_helper(const uint64_t server, std::vector<uint64_t>& servers)
    {
      std::vector<uint64_t>::iterator iter = std::find(servers.begin(), servers.end(), server);
      if (iter != servers.end())
      {
        servers.erase(iter);
      }
      return common::TFS_SUCCESS;
    }

    DataService::DataService():
        server_local_port_(-1),
        ns_ip_port_(0),
        sync_mirror_status_(0),
        max_cpu_usage_ (SYSPARAM_DATASERVER.max_cpu_usage_),
        tfs_ds_stat_ ("tfs-ds-stat"),
        do_check_thread_(0),
        task_thread_(0)
    {
      //init dataserver info
      memset(need_send_blockinfo_, 0, sizeof(need_send_blockinfo_));
      memset(&data_server_info_, 0, sizeof(DataServerStatInfo));
      memset(set_flag_, 0, sizeof(set_flag_));
      memset(hb_ip_port_, 0, sizeof(hb_ip_port_));
    }

    DataService::~DataService()
    {

    }

    int DataService::parse_common_line_args(int argc, char* argv[], std::string& errmsg)
    {
      char buf[256] = {'\0'};
      int32_t index = 0;
      while ((index = getopt(argc, argv, "i:")) != EOF)
      {
        switch (index)
        {
          case 'i':
            server_index_ = optarg;
            break;
          default:
            snprintf(buf, 256, "%c invalid parameter", index);
            break;
        }
      }
      int32_t iret = server_index_.empty() ? TFS_ERROR : TFS_SUCCESS;
      if (TFS_SUCCESS != iret)
      {
        snprintf(buf, 256, "server index in empty, invalid parameter");
      }
      errmsg.assign(buf);
      return iret;
    }

    int DataService::get_listen_port() const
    {
      int32_t port = -1;
      int32_t base_port = TBSYS_CONFIG.getInt(CONF_SN_PUBLIC, CONF_PORT, 0);
      if (base_port >= 1024 || base_port <= 65535)
      {
        port = SYSPARAM_DATASERVER.get_real_ds_port(base_port, server_index_);
        if (port < 1024 || base_port > 65535)
        {
          port = -1;
        }
      }
      return port;
    }

    const char* DataService::get_log_file_path()
    {
      const char* log_file_path = NULL;
      const char* work_dir = get_work_dir();
      if (work_dir != NULL)
      {
        log_file_path_ = work_dir;
        log_file_path_ += "/logs/dataserver";
        log_file_path_ = SYSPARAM_DATASERVER.get_real_file_name(log_file_path_, server_index_, "log");
        log_file_path = log_file_path_.c_str();
      }
      return log_file_path;
    }

    const char* DataService::get_pid_file_path()
    {
      const char* pid_file_path = NULL;
      const char* work_dir = get_work_dir();
      if (work_dir != NULL)
      {
        pid_file_path_ = work_dir;
        pid_file_path_ += "/logs/dataserver";
        pid_file_path_ = SYSPARAM_DATASERVER.get_real_file_name(pid_file_path_, server_index_, "pid");
        pid_file_path = pid_file_path_.c_str();
      }
      return pid_file_path;
    }

    string DataService::get_real_work_dir()
    {
      const char* work_dir = get_work_dir();
      string real_work_dir = "";
      if (NULL != work_dir)
      {
        real_work_dir = string(work_dir) + "/dataserver_" + server_index_;
      }
      return real_work_dir;
    }

    int DataService::initialize(int argc, char* argv[])
    {
      UNUSED(argc);
      int32_t iret = SYSPARAM_DATASERVER.initialize(config_file_, server_index_);
      if (TFS_SUCCESS != iret)
      {
        TBSYS_LOG(ERROR, "load dataserver parameter failed: %d", iret);
        iret = EXIT_GENERAL_ERROR;
      }

      if (TFS_SUCCESS == iret)
      {
        //create work directory
        string work_dir = get_real_work_dir();
        iret = work_dir.empty() ? EXIT_GENERAL_ERROR: TFS_SUCCESS;
        if (TFS_SUCCESS != iret)
        {
          TBSYS_LOG(ERROR, "%s not set work dir, must be exist", argv[0]);
        }
        if (TFS_SUCCESS == iret)
        {
          string storage_dir = work_dir + string("/storage");
          if (!DirectoryOp::create_full_path(storage_dir.c_str()))
          {
            iret = EXIT_GENERAL_ERROR;
            TBSYS_LOG(ERROR, "create directory %s error: %s", storage_dir.c_str(), strerror(errno));
          }
          if ( TFS_SUCCESS == iret)
          {
            storage_dir = work_dir + string("/tmp");
            if (!DirectoryOp::create_full_path(storage_dir.c_str()))
            {
              iret = EXIT_GENERAL_ERROR;
              TBSYS_LOG(ERROR, "create directory %s error: %s", storage_dir.c_str(), strerror(errno));
            }
          }
          if (TFS_SUCCESS == iret)
          {
            storage_dir = work_dir + string("/mirror");
            if (!DirectoryOp::create_full_path(storage_dir.c_str()))
            {
              iret = EXIT_GENERAL_ERROR;
              TBSYS_LOG(ERROR, "create directory %s error: %s", storage_dir.c_str(), strerror(errno));
            }
          }
        }
      }

      //set name server ip
      if (TFS_SUCCESS == iret)
      {
        iret = set_ns_ip();
      }

      //check dev & ip
      if (TFS_SUCCESS == iret)
      {
        const char* ip_addr = get_ip_addr();
        if (NULL == ip_addr)//get ip addr
        {
          iret =  EXIT_CONFIG_ERROR;
          TBSYS_LOG(ERROR, "%s", "dataserver not set ip_addr");
        }

        if (TFS_SUCCESS == iret)
        {
          const char *dev_name = get_dev();
          if (NULL == dev_name)//get dev name
          {
            iret =  EXIT_CONFIG_ERROR;
            TBSYS_LOG(ERROR, "%s","dataserver not set dev_name");
          }
          else
          {
            uint32_t ip_addr_id = tbsys::CNetUtil::getAddr(ip_addr);
            uint32_t local_ip   = Func::get_local_addr(dev_name);
            if (local_ip != ip_addr_id)
            {
              TBSYS_LOG(WARN, "ip '%s' is not local ip, local ip: %s",ip_addr, tbsys::CNetUtil::addrToString(local_ip).c_str());
              iret = EXIT_CONFIG_ERROR;
            }
          }
        }
      }

      if (TFS_SUCCESS == iret)
      {
        max_cpu_usage_  = SYSPARAM_DATASERVER.max_cpu_usage_;

        data_server_info_.startup_time_ = time(NULL);

        IpAddr* adr = reinterpret_cast<IpAddr*>(&data_server_info_.id_);
        adr->ip_ = tbsys::CNetUtil::getAddr(get_ip_addr());
        adr->port_ = get_listen_port();

        TBSYS_LOG(INFO, "dataserver listen port: %d", adr->port_);

        server_local_port_ = adr->port_;
        BasePacketStreamer* streamer = dynamic_cast<BasePacketStreamer*>(get_packet_streamer());
        if (NULL == streamer)
        {
          TBSYS_LOG(ERROR, "get packet streamer fail, stremer is null");
          iret = EXIT_GENERAL_ERROR;
        }
        else
        {
          char spec[32];
          int32_t second_listen_port = adr->port_ + 1;
          snprintf(spec, 32, "tcp::%d", second_listen_port);
          tbnet::IOComponent* com = transport_->listen(spec, get_packet_streamer(), this);
          if (NULL == com)
          {
            TBSYS_LOG(ERROR, "listen port: %d fail", second_listen_port);
            iret = EXIT_NETWORK_ERROR;
          }
          else
          {
            TBSYS_LOG(INFO, "listen second port: %d successful", second_listen_port);
          }
        }
        if (TFS_SUCCESS == iret)
        {
          ds_requester_.init(data_server_info_.id_, ns_ip_port_, &data_management_);
        }
      }

      if (TFS_SUCCESS == iret)
      {
        //init gcobject manager
        iret = GCObjectManager::instance().initialize(get_timer());
        if (TFS_SUCCESS != iret)
        {
          TBSYS_LOG(ERROR, "%s", "initialize gcobject manager fail");
          return iret;
        }

        //init global stat
        iret = stat_mgr_.initialize(get_timer());
        if (iret != TFS_SUCCESS)
        {
          TBSYS_LOG(ERROR, "%s", "initialize stat manager fail");
        }
        else
        {
          int64_t current = tbsys::CTimeUtil::getTime();
          StatEntry<string, string>::StatEntryPtr stat_ptr = new StatEntry<string, string>(tfs_ds_stat_, current, false);
          stat_ptr->add_sub_key("read-success");
          stat_ptr->add_sub_key("read-failed");
          stat_ptr->add_sub_key("write-success");
          stat_ptr->add_sub_key("write-failed");
          stat_ptr->add_sub_key("unlink-success");
          stat_ptr->add_sub_key("unlink-failed");
          stat_mgr_.add_entry(stat_ptr, SYSPARAM_DATASERVER.dump_stat_info_interval_);
        }

        if (TFS_SUCCESS == iret)
        {
          iret = data_management_.init_block_files(SYSPARAM_FILESYSPARAM);
          if (TFS_SUCCESS != iret)
          {
            TBSYS_LOG(ERROR, "dataservice::start, init block files fail! ret: %d\n", iret);
          }
          else
          {
            // sync mirror should init after bootstrap
            iret = init_sync_mirror();
            if (TFS_SUCCESS != iret)
            {
              TBSYS_LOG(ERROR, "dataservice::start, init sync mirror fail! \n");
              return iret;
            }
          }
        }

        if (TFS_SUCCESS == iret)
        {
          data_management_.get_ds_filesystem_info(data_server_info_.block_count_, data_server_info_.use_capacity_,
              data_server_info_.total_capacity_);
          data_server_info_.current_load_ = Func::get_load_avg();
          TBSYS_LOG(INFO,
              "block file status, block count: %d, use capacity: %" PRI64_PREFIX "d, total capacity: %" PRI64_PREFIX "d",
              data_server_info_.block_count_, data_server_info_.use_capacity_, data_server_info_.total_capacity_);

          block_checker_.init(data_server_info_.id_, &ds_requester_);
        }

        if (TFS_SUCCESS == iret)
        {
          iret = task_manager_.init(ns_ip_port_, data_server_info_.id_);
        }

        if (TFS_SUCCESS == iret)
        {
          //set write and read log
          const char* work_dir = get_work_dir();
          iret = NULL == work_dir ? TFS_ERROR : TFS_SUCCESS;
          if (TFS_SUCCESS != iret)
          {
            TBSYS_LOG(ERROR, "%s not set work directory", argv[0]);
          }
          else
          {
            read_stat_log_file_ = work_dir;
            read_stat_log_file_ += "/logs/read_stat";
            read_stat_log_file_ = SYSPARAM_DATASERVER.get_real_file_name(read_stat_log_file_, server_index_, "log");
            write_stat_log_file_= work_dir;
            write_stat_log_file_ += "/logs/write_stat";
            write_stat_log_file_  = SYSPARAM_DATASERVER.get_real_file_name(write_stat_log_file_, server_index_, "log");
            init_log_file(READ_STAT_LOGGER, read_stat_log_file_);
            init_log_file(WRITE_STAT_LOGGER, write_stat_log_file_);
            TBSYS_LOG(INFO, "dataservice start");
          }
        }

        if (TFS_SUCCESS == iret)
        {
          data_server_info_.status_ = DATASERVER_STATUS_ALIVE;
          for (int32_t i = 0; i < 2; i++)
          {
            heartbeat_thread_[i] = new HeartBeatThreadHelper(*this, i);
          }
          do_check_thread_  = new DoCheckThreadHelper(*this);
          task_thread_ = new TaskThreadHelper(*this);
        }
      }
      return iret;
    }

    int DataService::init_sync_mirror()
    {
      int ret = TFS_SUCCESS;
      //backup type:1.tfs 2.nfs
      int backup_type = SYSPARAM_DATASERVER.tfs_backup_type_;
      TBSYS_LOG(INFO, "backup type: %d\n", SYSPARAM_DATASERVER.tfs_backup_type_);

      char src_addr[common::MAX_ADDRESS_LENGTH];
      char dest_addr[common::MAX_ADDRESS_LENGTH];
      memset(src_addr, 0, common::MAX_ADDRESS_LENGTH);
      memset(dest_addr, 0, common::MAX_ADDRESS_LENGTH);

      // sync to tfs
      if (backup_type == SYNC_TO_TFS_MIRROR)
      {
        if (SYSPARAM_DATASERVER.local_ns_ip_.length() > 0 &&
            SYSPARAM_DATASERVER.local_ns_port_ != 0 &&
            SYSPARAM_DATASERVER.slave_ns_ip_.length() > 0)
        {
          // 1.if sync_mirror_ not empty, stop & clean
          sync_mirror_mutex_.lock();
          if (!sync_mirror_.empty())
          {
            vector<SyncBase*>::iterator iter = sync_mirror_.begin();
            for (; iter != sync_mirror_.end(); iter++)
            {
              (*iter)->stop();
              tbsys::gDelete(*iter);
            }
            sync_mirror_.clear();
          }
          // 2.init SyncBase
          snprintf(src_addr, MAX_ADDRESS_LENGTH, "%s:%d",
                   SYSPARAM_DATASERVER.local_ns_ip_.c_str(), SYSPARAM_DATASERVER.local_ns_port_);
          std::vector<std::string> slave_ns_ip;
          common::Func::split_string(SYSPARAM_DATASERVER.slave_ns_ip_.c_str(), '|', slave_ns_ip);
          for (uint8_t i = 0; i < slave_ns_ip.size(); i++)
          {
            // check slave_ns_ip valid
            snprintf(dest_addr, MAX_ADDRESS_LENGTH, "%s", slave_ns_ip.at(i).c_str());
            SyncBase* sync_base = new SyncBase(*this, backup_type, i, src_addr, dest_addr);
            // SyncBase init, will create thread
            ret = sync_base->init();
            if (TFS_SUCCESS != ret)
            {
              TBSYS_LOG(ERROR, "init sync base fail! local cluster ns addr: %s,slave cluster ns addr: %s\n",
                      src_addr, dest_addr);
              return ret;
            }
            sync_mirror_.push_back(sync_base);
          }
          sync_mirror_mutex_.unlock();
        }
      }
      return ret;
    }

    int DataService::set_ns_ip()
    {
      ns_ip_port_ = 0;
      IpAddr* adr = reinterpret_cast<IpAddr*> (&ns_ip_port_);
      uint32_t ip = Func::get_addr(SYSPARAM_DATASERVER.local_ns_ip_.c_str());
      if (0 == ip)
      {
        TBSYS_LOG(ERROR, "dataserver ip is error.");
      }
      else
      {
        adr->ip_ = ip;
        adr->port_ = SYSPARAM_DATASERVER.local_ns_port_;
      }

      const char* ip_list = SYSPARAM_DATASERVER.ns_addr_list_.c_str();
      if (NULL == ip_list)
      {
        TBSYS_LOG(ERROR, "dataserver real ip list is error");
      }
      else
      {
        std::vector <uint64_t> ns_ip_list;
        int32_t buffer_len = 256;
        char buffer[buffer_len];
        memset(buffer, 0, sizeof(buffer));
        strncpy(buffer, ip_list, buffer_len);
        char* t = NULL;
        char* s = buffer;
        while (NULL != (t = strsep(&s, "|")))
        {
          ns_ip_list.push_back(Func::get_addr(t));
        }

        if (ns_ip_list.size() < 1)
        {
          TBSYS_LOG(WARN, "must have one ns, check your ns' list");
          return TFS_ERROR;
        }

        if (ns_ip_list.size() != 2)
        {
          TBSYS_LOG(DEBUG, "must have two ns, check your ns' list");
          need_send_blockinfo_[1] = false;
        }

        for (uint32_t i = 0; i < ns_ip_list.size(); ++i)
        {
          adr = reinterpret_cast<IpAddr*>(&hb_ip_port_[i]);
          adr->ip_ = ns_ip_list[i];
          if (0 == adr->ip_)
          {
            TBSYS_LOG(ERROR, "dataserver real ip: %s list is error", ip_list);
            if (0 == i)
            {
              return TFS_ERROR;
            }
          }
          else
          {
            adr->port_ = (reinterpret_cast<IpAddr*>(&ns_ip_port_))->port_;
          }
          set_flag_[i] = true;

          if (1 == i)
          {
            break;
          }
        }
      }
      return TFS_SUCCESS;
    }

    int DataService::destroy_service()
    {
      data_server_info_.status_ = DATASERVER_STATUS_DEAD;
      //global stat destroy
      stat_mgr_.destroy();

      sync_mirror_mutex_.lock();
      vector<SyncBase*>::iterator iter = sync_mirror_.begin();
      for (; iter != sync_mirror_.end(); iter++)
      {
        (*iter)->stop();
        tbsys::gDelete(*iter);
      }
      sync_mirror_.clear();
      sync_mirror_mutex_.unlock();

      task_manager_.stop();
      block_checker_.stop();

      for (int i = 0; i < 2; i++)
      {
        if (0 != heartbeat_thread_[i])
        {
          heartbeat_thread_[i]->join();
          heartbeat_thread_[i] = 0;
        }
      }

      if (0 != do_check_thread_)
      {
        do_check_thread_->join();
        do_check_thread_ = 0;
      }

      if (0 != task_thread_)
      {
        task_thread_->join();
        task_thread_ = 0;
      }

      // destroy prefix op
      PhysicalBlock::destroy_prefix_op();

      GCObjectManager::instance().destroy();
      GCObjectManager::instance().wait_for_shut_down();
      return TFS_SUCCESS;
    }

    int DataService::run_heart(const int32_t who)
    {
      //sleep for a while, waiting for listen port establish
      int32_t iret = -1;
      int8_t heart_interval = DEFAULT_HEART_INTERVAL;
      sleep(heart_interval);
      while (!stop_)
      {
        TIMER_START();
        if (who % 2 == 0)
        {
          data_management_.get_ds_filesystem_info(data_server_info_.block_count_, data_server_info_.use_capacity_,
              data_server_info_.total_capacity_);
          data_server_info_.current_load_ = Func::get_load_avg();
          data_server_info_.current_time_ = time(NULL);
          cpu_metrics_.summary();
        }

        //if (DATASERVER_STATUS_DEAD== data_server_info_.status_)
        //  break;

          //目前超时时间设置成2s，主要是为了跟以及版本兼容，目前在心跳中还没有删除带Block以及其它的功能
          //这些功能在dataserver换完以后可以想办法将它删除， 然后将超时时间设置短点，失败重试次数多点,
          //同时目前心跳的间隔时间是写死的， 删除以上功能的同时，可以将心跳间隔由nameserver发给dataserver
          //也就是每次心跳时带上dataserver心跳的间隔时间

        iret = send_blocks_to_ns(heart_interval, who, 2000);//2s
        heart_interval = DEFAULT_HEART_INTERVAL;//这一行换成2.2版本的nameserver时可以删除
        if (TFS_SUCCESS != iret)
        {
          usleep(500000);  // if fail, sleep 500ms before retry
        }
        else
        {
          usleep(heart_interval * 1000000);
        }
        TIMER_END();
        TBSYS_LOG(DEBUG, "keppalive %s, cost time: %"PRI64_PREFIX"d", tbsys::CNetUtil::addrToString(hb_ip_port_[who]).c_str(),TIMER_DURATION());
      }
      return TFS_SUCCESS;
    }

    /*int DataService::stop_heart()
    {
      TBSYS_LOG(INFO, "stop heartbeat...");
      int8_t heart_interval = DEFAULT_HEART_INTERVAL;
      data_server_info_.status_ = DATASERVER_STATUS_DEAD;
      send_blocks_to_ns(heart_interval, 0,1500);
      send_blocks_to_ns(heart_interval, 1,1500);
      return TFS_SUCCESS;
    }*/

    int DataService::run_check()
    {
      int32_t last_rlog = 0;
      tzset();
      int zonesec = 86400 + timezone;
      while (!stop_)
      {
        time_t now = Func::get_monotonic_time();
        //check datafile
        data_management_.get_lease_manager().timeout(now);
        if (stop_)
          break;

        //check repair block
        block_checker_.consume_repair_task();
        if (stop_)
          break;

        //check task
        task_manager_.expire_task();
        if (stop_)
          break;

        //check clonedblock
        task_manager_.expire_cloned_block_map();
        if (stop_)
          break;

        int32_t current_time = time(NULL);
        // check log: write a new log everyday and expire error block
        if (current_time % 86400 >= zonesec && current_time % 86400 < zonesec + 300 && last_rlog < current_time - 600)
        {
          last_rlog = current_time;
          TBSYS_LOGGER.rotateLog(log_file_path_.c_str());
          READ_STAT_LOGGER.rotateLog(read_stat_log_file_.c_str());
          WRITE_STAT_LOGGER.rotateLog(write_stat_log_file_.c_str());

          // expire error block
          block_checker_.expire_error_block();
        }
        if (stop_)
          break;

        // check stat
        count_mutex_.lock();
        visit_stat_.check_visit_stat();
        count_mutex_.unlock();
        if (stop_)
          break;

        if (read_stat_buffer_.size() >= READ_STAT_LOG_BUFFER_LEN)
        {
          int64_t time_start = tbsys::CTimeUtil::getTime();
          TBSYS_LOG(INFO, "---->START DUMP READ INFO. buffer size: %zd, start time: %" PRI64_PREFIX "d", read_stat_buffer_.size(), time_start);
          read_stat_mutex_.lock();
          int per_log_size = FILE_NAME_LEN + 2; //two space
          char read_log_buffer[READ_STAT_LOG_BUFFER_LEN * per_log_size + 1];
          int32_t loops = read_stat_buffer_.size() / READ_STAT_LOG_BUFFER_LEN;
          int32_t remain = read_stat_buffer_.size() % READ_STAT_LOG_BUFFER_LEN;
          int32_t index = 0, offset;
          int32_t inner_loop = 0;
          //flush all record to log
          for (int32_t i = 0; i <= loops; ++i)
          {
            memset(read_log_buffer, 0, READ_STAT_LOG_BUFFER_LEN * per_log_size + 1);
            offset = 0;
            if (i == loops)
            {
              inner_loop = remain;
            }
            else
            {
              inner_loop = READ_STAT_LOG_BUFFER_LEN;
            }

            for (int32_t j = 0; j < inner_loop; ++j)
            {
              index = i * READ_STAT_LOG_BUFFER_LEN + j;
              FSName fs_name;
              fs_name.set_block_id(read_stat_buffer_[index].first);
              fs_name.set_file_id(read_stat_buffer_[index].second);
              /* per_log_size + 1: add null */
              snprintf(read_log_buffer + offset, per_log_size + 1, "  %s", fs_name.get_name());
              offset += per_log_size;
            }
            read_log_buffer[offset] = '\0';
            READ_STAT_LOG(INFO, "%s", read_log_buffer);
          }
          read_stat_buffer_.clear();
          read_stat_mutex_.unlock();
          int64_t time_end = tbsys::CTimeUtil::getTime();
          TBSYS_LOG(INFO, "Dump read info.end time: %" PRI64_PREFIX "d. Cost Time: %" PRI64_PREFIX "d\n", time_end, time_end - time_start);
        }

        sleep(SYSPARAM_DATASERVER.check_interval_);
      }
      return TFS_SUCCESS;
    }

    int DataService::post_message_to_server(BasePacket* message, const VUINT64& servers)
    {
      int32_t ret = servers.empty() ? EXIT_POST_MSG_RET_POST_MSG_ERROR : EXIT_POST_MSG_RET_NO_OTHER_MEMBER;
      if (EXIT_POST_MSG_RET_POST_MSG_ERROR != ret)
      {
        VUINT64 tmp(servers);
        send_msg_to_server_helper(data_server_info_.id_, tmp);
        if (!tmp.empty())
        {
          NewClient* client = NewClientManager::get_instance().create_client();
          ret = common::post_msg_to_server(tmp, client, message, ds_async_callback);
          if (TFS_SUCCESS != ret)
            ret = EXIT_POST_MSG_RET_POST_MSG_ERROR;
          else
            ret = tmp.size();
        }
      }
      return ret;
    }

    int DataService::callback(common::NewClient* client)
    {
      int32_t ret = NULL != client ? TFS_SUCCESS : TFS_ERROR;
      if (TFS_SUCCESS == ret)
      {
        NewClient::RESPONSE_MSG_MAP* sresponse = client->get_success_response();
        NewClient::RESPONSE_MSG_MAP* fresponse = client->get_fail_response();
        ret = NULL != sresponse && fresponse != NULL ? TFS_SUCCESS : TFS_ERROR;
        if (TFS_SUCCESS == ret)
        {
          tbnet::Packet* packet = client->get_source_msg();
          assert(NULL != packet);
          int32_t pcode = packet->getPCode();
          time_t now = Func::get_monotonic_time();
          bool all_success = sresponse->size() == client->get_send_id_sign().size();
          common::BasePacket* bpacket= dynamic_cast<BasePacket*>(packet);
          if (WRITE_DATA_MESSAGE == pcode)
          {
            WriteDataMessage* message = dynamic_cast<WriteDataMessage*>(bpacket);
            WriteDataInfo& info = message->get_write_info();
            LeaseId lease_id(info.file_number_, info.file_id_, info.block_id_);
            Lease* lease = get_data_management().get_lease_manager().get(lease_id, now);
            ret = NULL == lease ?  EXIT_DATA_FILE_ERROR : TFS_SUCCESS;
            if (TFS_SUCCESS == ret)
            {
              NewClient::RESPONSE_MSG_MAP_ITER iter = sresponse->begin();
              for (; iter != sresponse->end(); ++iter)
              {
                if (iter->second.second->getPCode() == RSP_WRITE_DATA_MESSAGE)
                {
                  WriteDataResponseMessage* msg = dynamic_cast<WriteDataResponseMessage*>(iter->second.second);
                  lease->update_member_info(msg->get_server(), msg->get_block_info(), msg->get_status());
                }
              }
              lease->dump(TBSYS_LOG_LEVEL_INFO, "write data complete, show result");
              StatusMessage* reply_msg = NULL;
              bool successful = lease->check_all_successful();
              TBSYS_LOG(INFO, "write data complete show result, all_success: %d, successful: %d",all_success, successful);
              if (all_success && successful)
              {
                TBSYS_LOG(INFO, "write data successful, block: %u, fileid: %"PRI64_PREFIX"u, lease id: %"PRI64_PREFIX"u, length: %d",
                  info.block_id_, info.file_id_, info.file_number_, info.length_);
                reply_msg = new StatusMessage(STATUS_MESSAGE_OK, "write data complete");
              }
              else
              {
                bool has_version_conflict = false;
                reply_msg = new StatusMessage(STATUS_MESSAGE_ERROR, "write data fail");
                if (0 == info.offset_)
                {
                  has_version_conflict = lease->check_has_version_conflict();
                  if (has_version_conflict)
                  {
                    ret = ds_requester_.resolve_block_version_conflict(lease, info.block_id_);
                    if (TFS_SUCCESS != ret)
                    {
                      lease->dump(TBSYS_LOG_LEVEL_INFO, "resolve block version conflict failed");
                    }
                    lease->reset_member_info();
                    data_management_.get_lease_manager().put(lease);
                    data_management_.get_lease_manager().remove(lease_id);
                    lease = NULL;
                  }
                }
                TBSYS_LOG(INFO, "write data failed, block: %u, fileid: %"PRI64_PREFIX"u, lease id: %"PRI64_PREFIX"u, length: %d, offset: %d, version conflict: %s",
                  info.block_id_, info.file_id_, info.file_number_, info.length_, info.offset_, has_version_conflict ? "yes" : "no");
                int32_t status = has_version_conflict ? EXIT_VERSION_CONFLICT_ERROR : TFS_ERROR;
                ret = ds_requester_.req_block_write_complete(info.block_id_, message->get_lease_id(), status);
                if (TFS_SUCCESS != ret)
                {
                  TBSYS_LOG(INFO, "write data failed and commit status to ns failed, block: %u, fileid: %"PRI64_PREFIX"u, lease id: %"PRI64_PREFIX"u, length: %d,offset: %d, version conflict: %s",
                    info.block_id_, info.file_id_, info.file_number_, info.length_, info.offset_, has_version_conflict ? "yes" : "no");
                }
              }
              ret = bpacket->reply(reply_msg);
            }
            data_management_.get_lease_manager().put(lease);
          }
          else if (RENAME_FILE_MESSAGE == pcode)
          {
            RenameFileMessage* message = dynamic_cast<RenameFileMessage*> (bpacket);
            uint32_t block_id = message->get_block_id();
            uint64_t file_id = message->get_file_id();
            uint64_t new_file_id = message->get_new_file_id();
            int32_t option_flag = message->get_option_flag();
            if (all_success && 0 == (option_flag & TFS_FILE_NO_SYNC_LOG))
            {
              if (sync_mirror_.size() > 0)
              {
                sync_mirror_.at(0)->write_sync_log(OPLOG_RENAME, block_id, new_file_id, file_id);
              }
            }
            else if (!all_success)
            {
              TBSYS_LOG(
                  ERROR,
                  "RenameFileMessage fail. chid: %d, blockid: %u, fileid: %" PRI64_PREFIX "u, new fileid: %" PRI64_PREFIX "u\n",
                  message->getChannelId(), block_id, file_id, new_file_id);
            }
            StatusMessage* reply_msg =  all_success ?
              new StatusMessage(STATUS_MESSAGE_OK, "rename complete"):
              new StatusMessage(STATUS_MESSAGE_ERROR, "rename fail");
            bpacket->reply(reply_msg);
          }
          else if (UNLINK_FILE_MESSAGE == pcode)
          {
            UnlinkFileMessage* message = dynamic_cast<UnlinkFileMessage*> (bpacket);
            LeaseId lease_id(message->get_lease_id_ext(), message->get_file_id(), message->get_block_id());
            Lease* lease = get_data_management().get_lease_manager().get(lease_id, now);
            ret = NULL == lease ?  EXIT_DATA_FILE_ERROR : TFS_SUCCESS;
            if (TFS_SUCCESS == ret)
            {
              NewClient::RESPONSE_MSG_MAP_ITER iter = sresponse->begin();
              for (; iter != sresponse->end(); ++iter)
              {
                if (iter->second.second->getPCode() == RSP_UNLINK_FILE_MESSAGE)
                {
                  UnlinkFileResponseMessage* msg = dynamic_cast<UnlinkFileResponseMessage*>(iter->second.second);
                  lease->update_member_info(msg->get_server(), msg->get_block_info(), msg->get_status());
                }
              }
              StatusMessage* reply_msg = NULL;
              bool successful = lease->check_all_successful();
              lease->dump(TBSYS_LOG_LEVEL_INFO, "unlink file complete, show result");
              TBSYS_LOG(INFO, "unlink file complete show result, all_success: %d, successful: %d",all_success, successful);
              if (all_success && successful)
              {
                TBSYS_LOG(INFO, "unlink file successful, block: %u, fileid: %"PRI64_PREFIX"u, lease id: %"PRI64_PREFIX"u, action: %d",
                  message->get_block_id(), message->get_file_id(), message->get_lease_id_ext(), message->get_unlink_type());
                reply_msg = new StatusMessage(STATUS_MESSAGE_OK, "unlink file complete");
                if (message->get_lease_id())
                  ds_requester_.req_block_write_complete(message->get_block_id(), message->get_lease_id(), ret, UNLINK_FLAG_YES);
              }
              else
              {
                reply_msg = new StatusMessage(STATUS_MESSAGE_ERROR, "unlink file fail");
                bool has_version_conflict = lease->check_has_version_conflict();
                if (has_version_conflict)
                {
                  ret = ds_requester_.resolve_block_version_conflict(lease, message->get_block_id());
                  if (TFS_SUCCESS != ret)
                  {
                    lease->dump(TBSYS_LOG_LEVEL_INFO, "resolve block version conflict failed");
                  }
                  lease->reset_member_info();
                }

                TBSYS_LOG(INFO, "unlink file failed, block: %u, fileid: %"PRI64_PREFIX"u, lease id: %"PRI64_PREFIX"u, action: %d, has_version_conflict: %s",
                  message->get_block_id(), message->get_file_id(), message->get_lease_id_ext(), message->get_unlink_type(), has_version_conflict ? "yes" : "no");
                int32_t status = has_version_conflict ? EXIT_VERSION_CONFLICT_ERROR : TFS_ERROR;
                if (message->get_lease_id())
                {
                  ds_requester_.req_block_write_complete(message->get_block_id(), message->get_lease_id(), status, UNLINK_FLAG_YES);
                  if (TFS_SUCCESS != ret)
                  {
                    TBSYS_LOG(INFO, "unlink file failed and commit status to ns failed, block: %u, fileid: %"PRI64_PREFIX"u, lease id: %"PRI64_PREFIX"u, action: %d, has_version_conflict: %s",
                      message->get_block_id(), message->get_file_id(), message->get_lease_id_ext(), message->get_unlink_type(), has_version_conflict ? "yes" : "no");
                  }
                }
              }
              ret = bpacket->reply(reply_msg);
            }
            data_management_.get_lease_manager().put(lease);
            data_management_.get_lease_manager().remove(lease_id);
          }
          else if (CLOSE_FILE_MESSAGE == pcode)
          {
            CloseFileMessage* message = dynamic_cast<CloseFileMessage*> (bpacket);
            CloseFileInfo close_file_info = message->get_close_file_info();
            int32_t lease_id = message->get_lease_id();
            uint64_t peer_id = message->get_connection()->getPeerId();

            //commit
            int32_t status = all_success ? TFS_SUCCESS : TFS_ERROR;
            int32_t iret = ds_requester_.req_block_write_complete(close_file_info.block_id_, lease_id, status);
            if (TFS_SUCCESS == status)
            {
              if (TFS_SUCCESS == iret)
              {
                //sync to mirror
                if (sync_mirror_.size() > 0)
                {
                  int option_flag = message->get_option_flag();
                  if (0 == (option_flag & TFS_FILE_NO_SYNC_LOG))
                  {
                    TBSYS_LOG(INFO, " write sync log, blockid: %u, fileid: %" PRI64_PREFIX "u", close_file_info.block_id_,
                        close_file_info.file_id_);
                    iret = sync_mirror_.at(0)->write_sync_log(OPLOG_INSERT, close_file_info.block_id_,
                        close_file_info.file_id_);
                  }
                }
              }
              if (TFS_SUCCESS == iret)
              {
                message->reply(new StatusMessage(STATUS_MESSAGE_OK));
              }
              else
              {
                TBSYS_LOG(ERROR,
                    "rep block write complete or write sync log fail, blockid: %u, fileid: %" PRI64_PREFIX "u, ret: %d",
                    close_file_info.block_id_, close_file_info.file_id_, iret);
                message->reply(new StatusMessage(STATUS_MESSAGE_ERROR));
              }
            }
            else
            {
              message->reply_error_packet(TBSYS_LOG_LEVEL(ERROR), iret,
                  "close write file to other dataserver fail, blockid: %u, fileid: %" PRI64_PREFIX "u, ret: %d",
                  close_file_info.block_id_, close_file_info.file_id_, iret);
            }
            TBSYS_LOG(INFO, "close file %s. filenumber: %" PRI64_PREFIX "u, blockid: %u, fileid: %" PRI64_PREFIX "u, peerip: %s, role: master",
                TFS_SUCCESS == iret ? "success" : "fail", close_file_info.file_number_, close_file_info.block_id_,
                close_file_info.file_id_, tbsys::CNetUtil::addrToString(peer_id).c_str());
            WRITE_STAT_LOG(INFO, "blockid: %u, fileid: %" PRI64_PREFIX "u, peerip: %s",
                close_file_info.block_id_, close_file_info.file_id_, tbsys::CNetUtil::addrToString(peer_id).c_str());

            string sub_key = "";
            TFS_SUCCESS == iret ? sub_key = "write-success" : sub_key = "write-failed";
            stat_mgr_.update_entry(tfs_ds_stat_, sub_key, 1);
          }
          else
          {
            TBSYS_LOG(ERROR, "callback handle error message pcode: %d", pcode);
          }
        }
      }
      return ret;
    }

    bool DataService::access_deny(BasePacket* message)
    {
      tbnet::Connection* conn = message->get_connection();
      if (!conn)
        return false;
      uint64_t peer_id = conn->getPeerId();
      int32_t type = message->getPCode();
      if (type == READ_DATA_MESSAGE || type == READ_DATA_MESSAGE_V2 ||
          type == READ_DATA_MESSAGE_V3)
        return acl_.deny(peer_id, AccessControl::READ);
      if (type == WRITE_DATA_MESSAGE || type == CLOSE_FILE_MESSAGE)
        return acl_.deny(peer_id, AccessControl::WRITE);
      if (type == UNLINK_FILE_MESSAGE)
        return acl_.deny(peer_id, AccessControl::UNLINK);
      return false;
    }

    tbnet::IPacketHandler::HPRetCode DataService::handlePacket(tbnet::Connection *connection, tbnet::Packet *packet)
    {
      tbnet::IPacketHandler::HPRetCode hret = tbnet::IPacketHandler::FREE_CHANNEL;
      bool bret = NULL != connection && NULL != packet;
      if (bret)
      {
        TBSYS_LOG(DEBUG, "receive pcode : %d", packet->getPCode());
        if (!packet->isRegularPacket())
        {
          bret = false;
          TBSYS_LOG(WARN, "control packet, pcode: %d, peer ip: %s", dynamic_cast<tbnet::ControlPacket*>(packet)->getCommand(),
               tbsys::CNetUtil::addrToString(connection->getPeerId()).c_str());
        }
        if (bret)
        {
          BasePacket* bpacket = dynamic_cast<BasePacket*>(packet);
          bpacket->set_connection(connection);
          bpacket->setExpireTime(MAX_RESPONSE_TIME);
          bpacket->set_direction(static_cast<DirectionStatus>(bpacket->get_direction()|DIRECTION_RECEIVE));

          if (bpacket->is_enable_dump())
          {
            bpacket->dump();
          }
          // add access control by message type
          if ((!access_deny(bpacket)) && (DATASERVER_STATUS_ALIVE == data_server_info_.status_))
          {
            bret = push(bpacket, false);
            if (bret)
              hret = tbnet::IPacketHandler::KEEP_CHANNEL;
            else
            {
              bpacket->reply_error_packet(TBSYS_LOG_LEVEL(ERROR),STATUS_MESSAGE_ERROR, "%s, task message beyond max queue size, discard, peer ip: %s",
                get_ip_addr(), tbsys::CNetUtil::addrToString(connection->getPeerId()).c_str());
              bpacket->free();
            }
          }
          else
          {
            bpacket->reply_error_packet(TBSYS_LOG_LEVEL(WARN), STATUS_MESSAGE_ACCESS_DENIED,
                "you client %s access been denied. msgtype: %d", tbsys::CNetUtil::addrToString(
                  connection->getPeerId()).c_str(), packet->getPCode());
            // packet denied, must free
            bpacket->free();
          }
        }
      }
      return hret;
    }

    bool DataService::handlePacketQueue(tbnet::Packet* packet, void* args)
    {
      bool bret = BaseService::handlePacketQueue(packet, args);
      if (bret)
      {
        int32_t pcode = packet->getPCode();
        int32_t ret = LOCAL_PACKET == pcode ? TFS_ERROR : TFS_SUCCESS;
        if (TFS_SUCCESS == ret)
        {
          switch (pcode)
          {
            case CREATE_FILENAME_MESSAGE:
              ret = create_file_number(dynamic_cast<CreateFilenameMessage*>(packet));
              break;
            case WRITE_DATA_MESSAGE:
              ret = write_data(dynamic_cast<WriteDataMessage*>(packet));
              break;
            case CLOSE_FILE_MESSAGE:
              ret = close_write_file(dynamic_cast<CloseFileMessage*>(packet));
              break;
            case WRITE_RAW_DATA_MESSAGE:
              ret = write_raw_data(dynamic_cast<WriteRawDataMessage*>(packet));
              break;
            case WRITE_INFO_BATCH_MESSAGE:
              ret = batch_write_info(dynamic_cast<WriteInfoBatchMessage*>(packet));
              break;
            case READ_DATA_MESSAGE_V2:
              ret = read_data_extra(dynamic_cast<ReadDataMessageV2*>(packet), READ_VERSION_2);
              break;
            case READ_DATA_MESSAGE_V3:
              ret = read_data_extra(dynamic_cast<ReadDataMessageV3*>(packet), READ_VERSION_3);
              break;
            case READ_DATA_MESSAGE:
              ret = read_data(dynamic_cast<ReadDataMessage*>(packet));
              break;
            case READ_RAW_DATA_MESSAGE:
              ret = read_raw_data(dynamic_cast<ReadRawDataMessage*>(packet));
              break;
            case FILE_INFO_MESSAGE:
              ret = read_file_info(dynamic_cast<FileInfoMessage*>(packet));
              break;
            case UNLINK_FILE_MESSAGE:
              ret = unlink_file(dynamic_cast<UnlinkFileMessage*>(packet));
              break;
            case RENAME_FILE_MESSAGE:
              ret = rename_file(dynamic_cast<RenameFileMessage*>(packet));
              break;
            case NEW_BLOCK_MESSAGE:
              ret = new_block(dynamic_cast<NewBlockMessage*>(packet));
              break;
            case REMOVE_BLOCK_MESSAGE:
              ret = remove_block(dynamic_cast<RemoveBlockMessage*>(packet));
              break;
            case LIST_BLOCK_MESSAGE:
              ret = list_blocks(dynamic_cast<ListBlockMessage*>(packet));
              break;
            case LIST_BITMAP_MESSAGE:
              ret = query_bit_map(dynamic_cast<ListBitMapMessage*>(packet));
              break;
            case REPLICATE_BLOCK_MESSAGE:
            case COMPACT_BLOCK_MESSAGE:
            case DS_COMPACT_BLOCK_MESSAGE:
            case DS_REPLICATE_BLOCK_MESSAGE:
            case RESP_DS_COMPACT_BLOCK_MESSAGE:
            case RESP_DS_REPLICATE_BLOCK_MESSAGE:
            case REQ_EC_MARSHALLING_MESSAGE:
            case REQ_EC_REINSTATE_MESSAGE:
            case REQ_EC_DISSOLVE_MESSAGE:
              ret = task_manager_.handle(dynamic_cast<BaseTaskMessage*>(packet));
              break;
            case CRC_ERROR_MESSAGE:
              ret = crc_error_cmd(dynamic_cast<CrcErrorMessage*>(packet));
              break;
            case GET_BLOCK_INFO_MESSAGE:
              ret = get_block_info(dynamic_cast<GetBlockInfoMessage*>(packet));
              break;
            case RESET_BLOCK_VERSION_MESSAGE:
              ret = reset_block_version(dynamic_cast<ResetBlockVersionMessage*>(packet));
              break;
            case GET_SERVER_STATUS_MESSAGE:
              ret = get_server_status(dynamic_cast<GetServerStatusMessage*>(packet));
              break;
            case RELOAD_CONFIG_MESSAGE:
              ret = reload_config(dynamic_cast<ReloadConfigMessage*>(packet));
              break;
            case STATUS_MESSAGE:
              ret = get_ping_status(dynamic_cast<StatusMessage*>(packet));
              break;
            case CLIENT_CMD_MESSAGE:
              ret = client_command(dynamic_cast<ClientCmdMessage*>(packet));
              break;
            case GET_DATASERVER_INFORMATION_MESSAGE:
              ret = get_dataserver_information(dynamic_cast<BasePacket*>(packet));
              break;
            case REQ_CALL_DS_REPORT_BLOCK_MESSAGE:
              ret = send_blocks_to_ns(dynamic_cast<BasePacket*>(packet));
              break;
            case REQ_CHECK_BLOCK_MESSAGE:
              ret = check_blocks(dynamic_cast<BasePacket*>(packet));
              break;
            case WRITE_RAW_INDEX_MESSAGE:
              ret = write_raw_index(dynamic_cast<WriteRawIndexMessage*>(packet));
              break;
            case READ_RAW_INDEX_MESSAGE:
              ret = read_raw_index(dynamic_cast<ReadRawIndexMessage*>(packet));
              break;
            case DEGRADE_READ_DATA_MESSAGE:
              ret = read_data_degrade(dynamic_cast<DegradeReadDataMessage*>(packet));
              break;
            default:
              TBSYS_LOG(ERROR, "process packet pcode: %d\n", pcode);
              ret = TFS_ERROR;
              break;
          }
          if (common::TFS_SUCCESS != ret)
          {
            common::BasePacket* msg = dynamic_cast<common::BasePacket*>(packet);
            msg->reply_error_packet(TBSYS_LOG_LEVEL(ERROR), ret, "execute message failed");
          }
        }
      }
      return bret;
    }

    int DataService::create_file_number(CreateFilenameMessage* message)
      {
      int32_t ret = NULL == message ? EXIT_PARAMETER_ERROR : TFS_SUCCESS;
      if (TFS_SUCCESS == ret)
      {
        TIMER_START();
        uint32_t block_id = message->get_block_id();
        uint64_t file_id = message->get_file_id();
        LogicBlock* logic_block = BlockFileManager::get_instance()->get_logic_block(block_id);
        ret = NULL == logic_block ? EXIT_NO_LOGICBLOCK_ERROR : TFS_SUCCESS;
        if (TFS_SUCCESS == ret)
        {
          if (0 == file_id)//这里需要修改客户端,如果客户端都升级到2.4版本以后，这块逻辑就可以删除
            ret = logic_block->open_write_file(file_id);
          else
            logic_block->reset_seq_id(file_id);
          if (TFS_SUCCESS == ret)
          {
            RespCreateFilenameMessage* resp_cfn_msg = new RespCreateFilenameMessage();
            resp_cfn_msg->set_block_id(block_id);
            resp_cfn_msg->set_file_id(file_id);
            resp_cfn_msg->set_file_number(get_data_management().get_lease_manager().gen_lease_id());
            ret = message->reply(resp_cfn_msg);
          }
        }
        TIMER_END();
        TBSYS_LOG(INFO,"create file id %s. blockid: %u, fileid: %" PRI64_PREFIX "u, cost time: %" PRI64_PREFIX "d",
          TFS_SUCCESS == ret ? "successful" : "fail", block_id, file_id, TIMER_DURATION());
      }
      if (TFS_SUCCESS != ret)
        stat_mgr_.update_entry(tfs_ds_stat_, "write-failed", 1);
      return ret;
    }

    int DataService::write_data(WriteDataMessage* message)
    {
      TIMER_START();
      int32_t ret = (NULL != message) ? TFS_SUCCESS : EXIT_PARAMETER_ERROR;
      if (TFS_SUCCESS == ret)
      {
        time_t now = Func::get_monotonic_time();
        WriteDataInfo& info= message->get_write_info();
        int32_t remote_version = message->get_block_version();
        VUINT64& servers = message->get_ds_list();
        bool is_master = Master_Server_Role == info.is_server_;
        TBSYS_LOG(DEBUG, "write data. block: %u, remote_version: %d, lease_id: %"PRI64_PREFIX"u, server size: %zd, offset: %d",
          info.block_id_, remote_version, info.file_number_, servers.size(), info.offset_);
        LeaseId lease_id(info.file_number_, info.file_id_, info.block_id_);
        Lease* lease = get_data_management().get_lease_manager().get(lease_id, now);
        if (NULL == lease && 0 == info.offset_)
        {
          //control lease size
          ret = get_data_management().get_lease_manager().has_out_of_limit() ? EXIT_DATAFILE_OVERLOAD : TFS_SUCCESS;
          if (TFS_SUCCESS != ret)
          {
            TBSYS_LOG(INFO, "block: %u, lease nums: %"PRI64_PREFIX"d is large than default: %d.",
                info.block_id_, get_data_management().get_lease_manager().size(), SYSPARAM_DATASERVER.max_datafile_nums_);
          }
          else
          {
            get_data_management().get_lease_manager().generation(lease_id, now, LEASE_TYPE_WRITE, servers);
            lease = get_data_management().get_lease_manager().get(lease_id, now);
            info.file_number_ = lease->lease_id_.lease_id_;
          }
        }
        ret = NULL == lease ? EXIT_DATA_FILE_ERROR : TFS_SUCCESS;
        if (TFS_SUCCESS != ret)
        {
          TBSYS_LOG(INFO, "lease is null mybe timeout or not found. block: %u, fileid: %" PRI64_PREFIX "u, lease id: %" PRI64_PREFIX "u",
              info.block_id_, info.file_id_, info.file_number_);
        }

        if (TFS_SUCCESS == ret)
        {
          //如果是主DATASERVER需要将数据转发给其他的DATASERVER
          if (is_master)
          {
            message->set_server(Slave_Server_Role);
            int32_t result = post_message_to_server(message, servers);
            ret = (result  == EXIT_POST_MSG_RET_POST_MSG_ERROR) ?  EXIT_SENDMSG_ERROR : TFS_SUCCESS;
            if (TFS_SUCCESS != ret)
            {
              TBSYS_LOG(INFO, "write data to other dataserver failed, block: %u, fileid: %"PRI64_PREFIX"u, version: %d, lease id: %"PRI64_PREFIX"u, role: master",
                info.block_id_, info.file_id_, remote_version, info.file_number_);
            }
          }
        }
        BlockInfo local_info;
        if (TFS_SUCCESS == ret)
        {
          const char* data = message->get_data();
          WriteLease* write_lease = dynamic_cast<WriteLease*>(lease);
          ret = get_data_management().write_data(local_info, write_lease, remote_version, info, data);
          if (is_master)//?????,??VERSION???�
            lease->update_member_info(data_server_info_.id_, local_info, ret);
          if (EXIT_NO_LOGICBLOCK_ERROR == ret)
          {
            TBSYS_LOG(INFO, "write data failed, block: %u is not exist, fileid: %"PRI64_PREFIX"u, lease_id: %"PRI64_PREFIX"u, offset: %d, ret: %d, role: %s",
              info.block_id_, info.file_id_, info.file_number_,info.offset_, ret, is_master ? "master" : "slave");
          }
          else if (EXIT_VERSION_CONFLICT_ERROR == ret)
          {
            TBSYS_LOG(INFO, "write data failed, block: %u version %d:%d conflict, fileid: %"PRI64_PREFIX"u, lease_id: %"PRI64_PREFIX"u, offset: %d, ret: %d, role: %s",
              info.block_id_, remote_version, local_info.version_, info.file_id_, info.file_number_, info.offset_, ret, is_master ? "master" : "slave");
          }
        }
        get_data_management().get_lease_manager().put(lease);
        if (is_master)
        {
          if (servers.size() <= 1U)
          {
            if (TFS_SUCCESS != ret)
              ds_requester_.req_block_write_complete(info.block_id_, message->get_lease_id(), ret);
            if (TFS_SUCCESS == ret)
            {
              ret = message->reply(new StatusMessage(STATUS_MESSAGE_OK));
            }
            else
            {
              char msg[64];
              snprintf(msg, 64, "write data result ret: %d", ret);
              stat_mgr_.update_entry(tfs_ds_stat_, "write-failed", 1);
              ret = message->reply(new StatusMessage(STATUS_MESSAGE_ERROR, msg));
            }
          }
        }
        else
        {
          //SLAVE DATASERVER回复MASTER DATASERVER
          WriteDataResponseMessage* reply_msg = new WriteDataResponseMessage();
          reply_msg->set_server(data_server_info_.id_);
          reply_msg->set_offset(info.offset_);
          reply_msg->set_block_info(local_info);
          reply_msg->set_status(ret);
          ret = message->reply(reply_msg);
        }
      }
      TIMER_END();
      return ret;
    }

    int DataService::close_write_file(CloseFileMessage* message)
    {
      TIMER_START();
      CloseFileInfo close_file_info = message->get_close_file_info();
      int32_t lease_id = message->get_lease_id();
      uint64_t peer_id = message->get_connection()->getPeerId();
      int32_t option_flag = message->get_option_flag();

      TBSYS_LOG(
          DEBUG,
          "close write file, blockid: %u, fileid: %" PRI64_PREFIX "u, filenumber: %" PRI64_PREFIX "u, leaseid: %u, from: %s\n",
          close_file_info.block_id_, close_file_info.file_id_, close_file_info.file_number_, lease_id,
          tbsys::CNetUtil::addrToString(peer_id).c_str());

      int ret = TFS_ERROR;
      if ((option_flag & TFS_FILE_CLOSE_FLAG_WRITE_DATA_FAILED))
      {
        if (CLOSE_FILE_SLAVER != close_file_info.mode_)
        {
          //commit
          ret = ds_requester_.req_block_write_complete(close_file_info.block_id_, lease_id, TFS_ERROR);
          if (TFS_SUCCESS == ret)
          {
            ret = message->reply(new StatusMessage(STATUS_MESSAGE_OK));
          }
          else
          {
            TBSYS_LOG(ERROR, "rep block write complete fail, blockid: %u, fileid: %"PRI64_PREFIX"d, ret: %d",
              close_file_info.block_id_, close_file_info.file_id_, ret);
            ret = message->reply(new StatusMessage(STATUS_MESSAGE_ERROR));
          }
        }
      }
      else
      {
        int32_t write_file_size = 0;
        ret = data_management_.close_write_file(close_file_info, write_file_size);
        if (TFS_SUCCESS != ret)
        {
          if (EXIT_DATAFILE_EXPIRE_ERROR == ret)
          {
            ret = message->reply_error_packet(
                TBSYS_LOG_LEVEL(ERROR), ret,
                "datafile is null(maybe expired). blockid: %u, fileid: %" PRI64_PREFIX "u, filenumber: %" PRI64_PREFIX "u, ret: %d",
                close_file_info.block_id_, close_file_info.file_id_, close_file_info.file_number_, ret);
          }
          else if (EXIT_NO_LOGICBLOCK_ERROR == ret)
          {
            ret = message->reply_error_packet(TBSYS_LOG_LEVEL(ERROR), ret,
                "close write file failed. block is not exist. blockid: %u, fileid: %" PRI64_PREFIX "u, ret: %d",
                close_file_info.block_id_, close_file_info.file_id_, ret);
          }
          else if (TFS_SUCCESS != ret)
          {
            try_add_repair_task(close_file_info.block_id_, ret);
            if (CLOSE_FILE_SLAVER != close_file_info.mode_)
            {
              ds_requester_.req_block_write_complete(close_file_info.block_id_, lease_id, ret);
            }
            ret = message->reply_error_packet(
                TBSYS_LOG_LEVEL(ERROR), ret,
                "close write file error. blockid: %u, fileid : %" PRI64_PREFIX "u, filenumber: %" PRI64_PREFIX "u. ret: %d",
                close_file_info.block_id_, close_file_info.file_id_, close_file_info.file_number_, ret);
          }
        }
        else
        {
          BlockInfo* blk = NULL;
          int32_t visit_count = 0;
          ret = data_management_.get_block_info(close_file_info.block_id_, blk, visit_count);
          if (TFS_SUCCESS != ret)
          {
            ret = message->reply_error_packet(TBSYS_LOG_LEVEL(ERROR), ret,
                "close write file failed. block is not exist. blockid: %u, fileid: %" PRI64_PREFIX "u, ret: %d",
                close_file_info.block_id_, close_file_info.file_id_, ret);
          }
          else
          {
            //if it is master DS. Send to other slave ds
            if (CLOSE_FILE_SLAVER != close_file_info.mode_)
            {
              do_stat(peer_id, write_file_size, write_file_size, 0, AccessStat::WRITE_BYTES);

              message->set_mode(CLOSE_FILE_SLAVER);
              message->set_block(blk);

              ret = post_message_to_server(message, message->get_ds_list());
              if (ret < 0)
              {
                // other ds failed, release lease
                ds_requester_.req_block_write_complete(close_file_info.block_id_, lease_id, TFS_ERROR);
                ret = message->reply_error_packet(TBSYS_LOG_LEVEL(ERROR), ret,
                    "close write file to other dataserver fail, blockid: %u, fileid: %" PRI64_PREFIX "u, ret: %d",
                    close_file_info.block_id_, close_file_info.file_id_, ret);
              }
              else if (ret == 0)//only self
              {
                //commit
                ret = ds_requester_.req_block_write_complete(close_file_info.block_id_, lease_id, TFS_SUCCESS);
                if (TFS_SUCCESS == ret)
                {
                  //sync to mirror
                  if (sync_mirror_.size() > 0)
                  {
                    if (0 == (option_flag & TFS_FILE_NO_SYNC_LOG))
                    {
                      TBSYS_LOG(INFO, " write sync log, blockid: %u, fileid: %" PRI64_PREFIX "u", close_file_info.block_id_,
                          close_file_info.file_id_);
                      ret = sync_mirror_.at(0)->write_sync_log(OPLOG_INSERT, close_file_info.block_id_,
                          close_file_info.file_id_);
                    }
                  }
                }

                if (TFS_SUCCESS == ret)
                {
                  ret = message->reply(new StatusMessage(STATUS_MESSAGE_OK));
                }
                else
                {
                  TBSYS_LOG(ERROR,
                      "rep block write complete or write sync log fail, blockid: %u, fileid: %" PRI64_PREFIX "u, ret: %d",
                      close_file_info.block_id_, close_file_info.file_id_, ret);
                  ret = message->reply(new StatusMessage(STATUS_MESSAGE_ERROR));
                }
                TIMER_END();
                TBSYS_LOG(INFO, "close file %s. filenumber: %" PRI64_PREFIX "u, blockid: %u, fileid: %" PRI64_PREFIX "u, peerip: %s, role: %s, cost time: %" PRI64_PREFIX "d",
                    TFS_SUCCESS == ret ? "success" : "fail", close_file_info.file_number_, close_file_info.block_id_,
                    close_file_info.file_id_, tbsys::CNetUtil::addrToString(peer_id).c_str(),
                    CLOSE_FILE_SLAVER != close_file_info.mode_ ? "master" : "slave", TIMER_DURATION());
                WRITE_STAT_LOG(INFO, "blockid: %u, fileid: %" PRI64_PREFIX "u, peerip: %s",
                    close_file_info.block_id_, close_file_info.file_id_, tbsys::CNetUtil::addrToString(peer_id).c_str());

                string sub_key = "";
                TFS_SUCCESS == ret ? sub_key = "write-success" : sub_key = "write-failed";
                stat_mgr_.update_entry(tfs_ds_stat_, sub_key, 1);
              }
              else
              {
                // post success.
                ret = TFS_SUCCESS;
              }
            }
            else
            {
              //slave will save seqno to prevent from the conflict when this block change to master block
              const BlockInfo* copyblk = message->get_block();
              if (NULL != copyblk)
              {
                blk->seq_no_ = copyblk->seq_no_;
              }
              ret = message->reply(new StatusMessage(STATUS_MESSAGE_OK));
              TIMER_END();
              TBSYS_LOG(INFO, "close file %s. filenumber: %" PRI64_PREFIX "u, blockid: %u, fileid: %" PRI64_PREFIX "u, peerip: %s, role: %s, cost time: %" PRI64_PREFIX "d",
                  TFS_SUCCESS == ret ? "success" : "fail", close_file_info.file_number_, close_file_info.block_id_,
                  close_file_info.file_id_, tbsys::CNetUtil::addrToString(peer_id).c_str(),
                  CLOSE_FILE_SLAVER != close_file_info.mode_ ? "master" : "slave", TIMER_DURATION());
              WRITE_STAT_LOG(INFO, "blockid: %u, fileid: %" PRI64_PREFIX "u, peerip: %s",
                  close_file_info.block_id_, close_file_info.file_id_, tbsys::CNetUtil::addrToString(peer_id).c_str());

              string sub_key = "";
              TFS_SUCCESS == ret ? sub_key = "write-success" : sub_key = "write-failed";
              stat_mgr_.update_entry(tfs_ds_stat_, sub_key, 1);
            }
          }
        }

        // hook to be checked on write or update
        if (TFS_SUCCESS == ret)
        {
          check_block_.add_check_task(close_file_info.block_id_);
        }
      }

      return ret;
    }

    int DataService::read_data_extra(ReadDataMessageV2* message, int32_t version)
    {
      int ret = TFS_ERROR;
      TIMER_START();
      RespReadDataMessageV2* resp_rd_v2_msg = NULL;
      if (READ_VERSION_2 == version)
      {
        resp_rd_v2_msg = new RespReadDataMessageV2();
      }
      else if (READ_VERSION_3 == version)
      {
        resp_rd_v2_msg = new RespReadDataMessageV3();
      }
      else
      {
        TBSYS_LOG(ERROR, "unknown read version type %d", version);
        return TFS_ERROR;
      }

      uint32_t block_id = message->get_block_id();
      uint64_t file_id = message->get_file_id();
      int32_t read_len = message->get_length();
      int32_t read_offset = message->get_offset();
      uint64_t peer_id = message->get_connection()->getPeerId();
      int8_t flag = message->get_flag();
      const FamilyInfoExt& family_info = message->get_family_info();

      TBSYS_LOG(DEBUG, "blockid: %u, fileid: %" PRI64_PREFIX "u, read len: %d, read offset: %d, resp: %p", block_id,
                file_id, read_len, read_offset, resp_rd_v2_msg);
      //add FileInfo size if the first fragment
      int32_t real_read_len = 0;
      if (0 == read_offset)
      {
        real_read_len = read_len + FILEINFO_SIZE;
      }
      else if (read_offset > 0) //not the first fragment
      {
        real_read_len = read_len;
        read_offset += FILEINFO_SIZE;
      }
      else
      {
        TBSYS_LOG(ERROR, "read data failed, invalid offset:%d", read_offset);
      }

      if (real_read_len > 0)
      {
        char* tmp_data_buffer = new char[real_read_len];
        if (INVALID_FAMILY_ID == family_info.family_id_)
        {
          ret = data_management_.read_data(block_id, file_id, read_offset, flag, real_read_len, tmp_data_buffer);
        }
        else
        {
          ret = data_management_.read_data_degrade(block_id, file_id, read_offset, flag, real_read_len,
              tmp_data_buffer, family_info);
        }
        if (TFS_SUCCESS != ret)
        {
          try_add_repair_task(block_id, ret);
          tbsys::gDeleteA(tmp_data_buffer);
          resp_rd_v2_msg->set_length(ret);
          message->reply(resp_rd_v2_msg);
        }
        else
        {
          if (0 == read_offset)
          {
            real_read_len -= FILEINFO_SIZE;
          }

          int32_t visit_file_size = reinterpret_cast<FileInfo*>(tmp_data_buffer)->size_;
          char* packet_data = resp_rd_v2_msg->alloc_data(real_read_len);
          if (0 != real_read_len)
          {
            if (NULL == packet_data)
            {
              tbsys::gDelete(resp_rd_v2_msg);
              tbsys::gDeleteA(tmp_data_buffer);
              TBSYS_LOG(ERROR, "alloc data failed, blockid: %u, fileid: %" PRI64_PREFIX "u, real len: %d", block_id,
                        file_id, real_read_len);
              ret = TFS_ERROR;
              message->reply_error_packet(TBSYS_LOG_LEVEL(ERROR), ret, "dataserver memory insufficient");
            }

            if (TFS_SUCCESS == ret)
            {
              if (0 == read_offset)
              {
                //set FileInfo
                reinterpret_cast<FileInfo*>(tmp_data_buffer)->size_ -= FILEINFO_SIZE;
                resp_rd_v2_msg->set_file_info(reinterpret_cast<FileInfo*>(tmp_data_buffer));
                memcpy(packet_data, tmp_data_buffer + FILEINFO_SIZE, real_read_len);
              }
              else
              {
                memcpy(packet_data, tmp_data_buffer, real_read_len);
              }
            }
          }

          if (TFS_SUCCESS == ret)
          {
            //set to connection
            message->reply(resp_rd_v2_msg);
            tbsys::gDeleteA(tmp_data_buffer);
            do_stat(peer_id, visit_file_size, real_read_len, read_offset, AccessStat::READ_BYTES);
          }
        }
      }
      else
      {
        ret = EXIT_INVALID_ARGU_ERROR;
        resp_rd_v2_msg->set_length(ret);
        message->reply(resp_rd_v2_msg);
      }

      TIMER_END();
      TBSYS_LOG(INFO, "read v%d %s. blockid: %u, fileid: %" PRI64_PREFIX "u, read len: %d, read offset: %d, peer ip: %s, cost time: %" PRI64_PREFIX "d",
                version, TFS_SUCCESS == ret ? "success" : "fail",
                block_id, file_id, real_read_len, read_offset,
                tbsys::CNetUtil::addrToString(peer_id).c_str(), TIMER_DURATION());

      string sub_key = "";
      TFS_SUCCESS == ret ? sub_key = "read-success": sub_key = "read-failed";
      stat_mgr_.update_entry(tfs_ds_stat_, sub_key, 1);

      read_stat_mutex_.lock();
      read_stat_buffer_.push_back(make_pair(block_id, file_id));
      read_stat_mutex_.unlock();
      return TFS_SUCCESS;
    }

    int DataService::read_data(ReadDataMessage* message)
    {
      int ret = TFS_ERROR;
      TIMER_START();
      RespReadDataMessage* resp_rd_msg = new RespReadDataMessage();
      uint32_t block_id = message->get_block_id();
      uint64_t file_id = message->get_file_id();
      int32_t read_len = message->get_length();
      int32_t read_offset = message->get_offset();
      uint64_t peer_id = message->get_connection()->getPeerId();
      int8_t flag = message->get_flag();
      const FamilyInfoExt& family_info = message->get_family_info();

      //add FileInfo if the first fragment
      int32_t real_read_len = 0;
      if (0 == read_offset)
      {
        real_read_len = read_len + FILEINFO_SIZE;
      }
      else if (read_offset > 0)//not the first fragment
      {
        real_read_len = read_len;
        read_offset += FILEINFO_SIZE;
      }
      else
      {
        TBSYS_LOG(ERROR, "read data failed, invalid offset:%d", read_offset);
      }

      if (real_read_len > 0)
      {
        char* tmp_data_buffer = new char[real_read_len];
        if (INVALID_FAMILY_ID == family_info.family_id_)
        {
          ret = data_management_.read_data(block_id, file_id, read_offset, flag, real_read_len, tmp_data_buffer);
        }
        else
        {
          ret = data_management_.read_data_degrade(block_id, file_id, read_offset, flag, real_read_len,
              tmp_data_buffer, family_info);
        }
        if (TFS_SUCCESS != ret)
        {
          try_add_repair_task(block_id, ret);
          tbsys::gDeleteA(tmp_data_buffer);
          resp_rd_msg->set_length(ret);
          message->reply(resp_rd_msg);
        }
        else
        {
          if (0 == read_offset)
          {
            real_read_len -= FILEINFO_SIZE;
          }

          int32_t visit_file_size = reinterpret_cast<FileInfo *>(tmp_data_buffer)->size_;
          char* packet_data = resp_rd_msg->alloc_data(real_read_len);
          if (0 != real_read_len)
          {
            if (NULL == packet_data)
            {
              tbsys::gDelete(resp_rd_msg);
              tbsys::gDeleteA(tmp_data_buffer);
              TBSYS_LOG(ERROR, "alloc data failed, blockid: %u, fileid: %" PRI64_PREFIX "u, real len: %d",
                  block_id, file_id, real_read_len);
              ret = TFS_ERROR;
              message->reply_error_packet(TBSYS_LOG_LEVEL(ERROR), ret, "dataserver memory insufficient");
            }

            if (TFS_SUCCESS == ret)
            {
              if (0 == read_offset)
              {
                memcpy(packet_data, tmp_data_buffer + FILEINFO_SIZE, real_read_len);
              }
              else
              {
                memcpy(packet_data, tmp_data_buffer, real_read_len);
              }
            }
          }

          if (TFS_SUCCESS == ret)
          {
            // set to connection
            message->reply(resp_rd_msg);
            tbsys::gDeleteA(tmp_data_buffer);
            do_stat(peer_id, visit_file_size, real_read_len, read_offset, AccessStat::READ_BYTES);
          }
        }
      }
      else
      {
        ret = EXIT_INVALID_ARGU_ERROR;
        resp_rd_msg->set_length(ret);
        message->reply(resp_rd_msg);
      }

      TIMER_END();
      TBSYS_LOG(INFO, "read %s. blockid: %u, fileid: %" PRI64_PREFIX "u, read len: %d, read offset: %d, peer ip: %s, cost time: %" PRI64_PREFIX "d",
          TFS_SUCCESS == ret ? "success" : "fail", block_id, file_id, real_read_len, read_offset,
          tbsys::CNetUtil::addrToString(peer_id).c_str(), TIMER_DURATION());

      string sub_key = "";
      TFS_SUCCESS == ret ? sub_key = "read-success": sub_key = "read-failed";
      stat_mgr_.update_entry(tfs_ds_stat_, sub_key, 1);

      read_stat_mutex_.lock();
      read_stat_buffer_.push_back(make_pair(block_id, file_id));
      read_stat_mutex_.unlock();

      return TFS_SUCCESS;
    }

    int DataService::read_data_degrade(DegradeReadDataMessage* message)
    {
      int ret = TFS_SUCCESS;
      TIMER_START();
      RespReadDataMessageV2 *resp_rd_msg = new RespReadDataMessageV2();
      uint32_t block_id = message->get_block_id();
      uint64_t file_id = message->get_file_id();
      int32_t read_len = message->get_length();
      int32_t read_offset = message->get_offset();
      uint64_t peer_id = message->get_connection()->getPeerId();
      int8_t flag = message->get_flag();
      const FamilyInfoExt& family_info = message->get_family_info();

      // add FileInfo if the first fragment
      // here we transfer offset&length to comparatively FileInfo
      int32_t visit_file_size = 0;
      int32_t real_read_len = 0;
      if (read_offset == 0 && read_len >= 0) // length 0 denotes stat
      {
        real_read_len = read_len + FILEINFO_SIZE;
      }
      else if (read_offset > 0 && read_len > 0)
      {
        real_read_len = read_len;
        read_offset += FILEINFO_SIZE;
      }
      else
      {
        ret = EXIT_INVALID_ARGU_ERROR;
        TBSYS_LOG(ERROR, "read data failed, invalid offset:%d", read_offset);
      }

      if (TFS_SUCCESS == ret)
      {
        char* tmp_data_buffer = resp_rd_msg->alloc_data(real_read_len);
        if (NULL == tmp_data_buffer)
        {
          ret = EXIT_NO_MEMORY;
          TBSYS_LOG(ERROR, "degrade read data, alloca data fail.");
        }
        else
        {
          ret = data_management_.read_data_degrade(block_id, file_id, read_offset, flag,
              real_read_len, tmp_data_buffer, family_info);
          if (TFS_SUCCESS == ret)
          {
            if (0 == read_offset)
            {
              real_read_len -= FILEINFO_SIZE;
            }

            FileInfo* file_info =  reinterpret_cast<FileInfo *>(tmp_data_buffer);
            visit_file_size = file_info->size_;
            if (0 == read_offset)
            {
              resp_rd_msg->set_file_info(file_info);
              memmove(tmp_data_buffer, tmp_data_buffer + FILEINFO_SIZE, real_read_len);
            }
          }
        }
      }

      if (TFS_SUCCESS == ret)
      {
        do_stat(peer_id, visit_file_size, real_read_len, read_offset, AccessStat::READ_BYTES);
        resp_rd_msg->set_length(real_read_len);
      }
      else
      {
        try_add_repair_task(block_id, ret);
        resp_rd_msg->set_length(ret);
      }

      message->reply(resp_rd_msg);

      TIMER_END();
      TBSYS_LOG(INFO, "degrade read %s. blockid: %u, fileid: %" PRI64_PREFIX "u, read len: %d, read offset: %d, peer ip: %s, cost time: %" PRI64_PREFIX "d",
          TFS_SUCCESS == ret ? "success" : "fail", block_id, file_id, real_read_len, read_offset,
          tbsys::CNetUtil::addrToString(peer_id).c_str(), TIMER_DURATION());

      string sub_key = "";
      TFS_SUCCESS == ret ? sub_key = "read-success": sub_key = "read-failed";
      stat_mgr_.update_entry(tfs_ds_stat_, sub_key, 1);

      read_stat_mutex_.lock();
      read_stat_buffer_.push_back(make_pair(block_id, file_id));
      read_stat_mutex_.unlock();

      return TFS_SUCCESS;

    }

    int DataService::read_raw_data(ReadRawDataMessage* message)
    {
      RespReadRawDataMessage* resp_rrd_msg = new RespReadRawDataMessage();
      uint32_t block_id = message->get_block_id();
      int32_t read_len = message->get_length();
      int32_t read_offset = message->get_offset();

      TBSYS_LOG(DEBUG, "blockid: %u read data batch, read size: %d, offset: %d", block_id, read_len, read_offset);

      char* tmp_data_buffer = new char[read_len];
      int32_t real_read_len = read_len;
      int32_t data_file_size = 0;
      int ret = data_management_.read_raw_data(block_id, read_offset, real_read_len, tmp_data_buffer, data_file_size);
      if (TFS_SUCCESS != ret)
      {
        try_add_repair_task(block_id, ret);
        tbsys::gDeleteA(tmp_data_buffer);
        resp_rrd_msg->set_length(ret);
        message->reply(resp_rrd_msg);
        return TFS_SUCCESS;
      }

      char* packet_data = resp_rrd_msg->alloc_data(real_read_len);
      if (0 != real_read_len)
      {
        if (NULL == packet_data)
        {
          tbsys::gDelete(resp_rrd_msg);
          tbsys::gDeleteA(tmp_data_buffer);
          TBSYS_LOG(ERROR, "allocdata fail, blockid: %u, realreadlen: %d", block_id, real_read_len);
          return TFS_ERROR;
        }
        else
        {
          memcpy(packet_data, tmp_data_buffer, real_read_len);
        }
      }
      message->set_length(real_read_len);  // ??
      resp_rrd_msg->set_data_file_size(data_file_size);
      message->reply(resp_rrd_msg);
      tbsys::gDeleteA(tmp_data_buffer);

      do_stat(0, 0, real_read_len, read_offset, AccessStat::READ_COUNT);

      return TFS_SUCCESS;
    }

    int DataService::read_file_info(FileInfoMessage* message)
    {
      TIMER_START();
      uint32_t block_id = message->get_block_id();
      uint64_t file_id = message->get_file_id();
      int32_t mode = message->get_mode();
      const FamilyInfoExt& family_info = message->get_family_info();

      TBSYS_LOG(DEBUG, "read file info, blockid: %u, fileid: %" PRI64_PREFIX "u, mode: %d",
          block_id, file_id, mode);

      int ret = TFS_SUCCESS;
      FileInfo finfo;
      if (INVALID_FAMILY_ID == family_info.family_id_)
      {
        ret = data_management_.read_file_info(block_id, file_id, mode, finfo);
      }
      else
      {
        int8_t stat_flag = mode;
        int32_t read_len = FILEINFO_SIZE;
        ret = data_management_.read_data_degrade(block_id, file_id, 0, stat_flag, read_len,
            reinterpret_cast<char*>(&finfo), family_info);
      }

      if (TFS_SUCCESS != ret)
      {
        try_add_repair_task(block_id, ret);
        return message->reply_error_packet(TBSYS_LOG_LEVEL(ERROR), ret,
            "readfileinfo fail, blockid: %u, fileid: %" PRI64_PREFIX "u, ret: %d", block_id, file_id, ret);
      }

      RespFileInfoMessage* resp_fi_msg = new RespFileInfoMessage();
      resp_fi_msg->set_file_info(&finfo);
      message->reply(resp_fi_msg);

      TIMER_END();
      TBSYS_LOG(DEBUG, "read fileinfo %s. blockid: %u, fileid: %" PRI64_PREFIX "u, mode: %d, cost time: %" PRI64_PREFIX "d",
          TFS_SUCCESS == ret ? "success" : "fail", block_id, file_id, mode, TIMER_DURATION());
      return TFS_SUCCESS;
    }

    int DataService::rename_file(RenameFileMessage* message)
    {
      uint32_t block_id = message->get_block_id();
      uint64_t file_id = message->get_file_id();
      uint64_t new_file_id = message->get_new_file_id();
      TBSYS_LOG(INFO,
          "renamefile, blockid: %u, fileid: %" PRI64_PREFIX "u, newfileid: %" PRI64_PREFIX "u, ds list size: %zd",
          block_id, file_id, new_file_id, message->get_ds_list().size());

      int ret = data_management_.rename_file(block_id, file_id, new_file_id);
      if (TFS_SUCCESS != ret)
      {
        try_add_repair_task(block_id, ret);
        return message->reply_error_packet(TBSYS_LOG_LEVEL(ERROR), ret,
            "rename file fail, blockid: %u, fileid: %" PRI64_PREFIX "u, newfileid: %" PRI64_PREFIX "u, ret: %d",
            block_id, file_id, new_file_id, ret);
      }

      //is master
      bool is_master = false;
      if (0 == (message->is_server() & 1))
      {
        is_master = true;
      }
      // send to other ds
      if (is_master)
      {
        message->set_server();
        ret = post_message_to_server(message, message->get_ds_list());
        if (ret >= 0)
        {
          if (0 == ret)
          {
            message->reply(new StatusMessage(STATUS_MESSAGE_OK));
          }
          return TFS_SUCCESS;
        }
        else
        {
          return message->reply_error_packet(TBSYS_LOG_LEVEL(ERROR), ret,
              "renamefile to other dataserver");
        }
      }

      // return
      message->reply(new StatusMessage(STATUS_MESSAGE_OK));
      return TFS_SUCCESS;
    }

    int DataService::unlink_file(UnlinkFileMessage* message)
    {
      TIMER_START();
      int32_t ret = NULL == message ? EXIT_PARAMETER_ERROR : TFS_SUCCESS;
      if (TFS_SUCCESS == ret)
      {
        uint32_t block_id = message->get_block_id();
        uint64_t file_id = message->get_file_id();
        int32_t option_flag = message->get_option_flag();
        int32_t action = message->get_unlink_type();
        uint64_t peer_id = message->get_connection()->getPeerId();
        const VUINT64& servers = message->get_ds_list();
        int32_t remote_version = message->get_block_version();
        bool is_master = (message->get_server() & 1) == 0;
        int64_t file_size = 0;
        int32_t unlink_flag = 0;
        Lease* lease = NULL;
        const FamilyInfoExt& family_info = message->get_family_info();
        const int32_t data_num = GET_DATA_MEMBER_NUM(family_info.family_aid_info_);
        const int32_t check_num = GET_CHECK_MEMBER_NUM(family_info.family_aid_info_);

        VUINT64 pservers;
        if (INVALID_FAMILY_ID == family_info.family_id_)
        {
          pservers = servers;
        }
        else
        {
          pservers.push_back(data_server_info_.id_);
          for (int32_t i = 0; i < check_num; i++)
          {
            pservers.push_back(family_info.members_[i+data_num].second);
          }
        }

        TBSYS_LOG(DEBUG, "unlink file. family_id: %"PRI64_PREFIX"d, block: %u, file_id: %"PRI64_PREFIX"u, remote_version: %d, lease_id: %u, server size: %zd, master: %s",
          family_info.family_id_, block_id, file_id, remote_version, message->get_lease_id(), pservers.size(), is_master ? "yes" : "no");
        LeaseId lease_id(INVALID_LEASE_ID, file_id, block_id);
        if (is_master)
        {
          time_t now = Func::get_monotonic_time();
          get_data_management().get_lease_manager().generation(lease_id, now, LEASE_TYPE_UNLINK,pservers);
          lease = get_data_management().get_lease_manager().get(lease_id, now);
          message->set_lease_id_ext(lease->lease_id_.lease_id_);
          message->set_server();

          int32_t result = post_message_to_server(message, servers);  // just post to real servers
          ret = (result  == EXIT_POST_MSG_RET_POST_MSG_ERROR) ?  EXIT_SENDMSG_ERROR : TFS_SUCCESS;

          if (TFS_SUCCESS != ret)
          {
            TBSYS_LOG(INFO, "unlink file forwarding message to other dataserver failed, block: %u,"
                "fileid: %"PRI64_PREFIX"u,lease id: %"PRI64_PREFIX"u, role : master, ret: %d",
                block_id, file_id, lease->lease_id_.lease_id_, ret);
          }
        }

        BlockInfo local_info;
        if (TFS_SUCCESS == ret)
        {
          if (!is_master && (INVALID_FAMILY_ID != family_info.family_id_))
          {
            uint32_t target_block_id = INVALID_BLOCK_ID;
            for (int32_t i = 0; i < check_num; i++)
            {
              if (data_server_info_.id_ == family_info.members_[i+data_num].second)
              {
                target_block_id = family_info.members_[i+data_num].first;
                break;
              }
            }

            if (INVALID_BLOCK_ID == target_block_id)
            {
              ret = EXIT_INVALID_ARGU_ERROR;
            }
            else
            {
              ret = get_data_management().unlink_file_parity(target_block_id, block_id, file_id, message->get_unlink_flag(), file_size);
            }
          }
          else
          {
            ret = get_data_management().unlink_file(local_info, file_size, block_id, file_id, action, remote_version, unlink_flag);
          }

          if (is_master)
          {
            lease->update_member_info(data_server_info_.id_, local_info, ret);
            get_data_management().get_lease_manager().put(lease);
          }
          if (TFS_SUCCESS == ret)
          {
            //sync log
            if (is_master && (0 == (option_flag & TFS_FILE_NO_SYNC_LOG)) && (!sync_mirror_.empty()))
            {
              TBSYS_LOG(DEBUG, "master dataserver: delete synclog. blockid: %d, fileid: %" PRI64_PREFIX "u, action: %d\n",
                  block_id, file_id, action);
              sync_mirror_.at(0)->write_sync_log(OPLOG_REMOVE, block_id, file_id, action);
            }
          }
          else if (EXIT_NO_LOGICBLOCK_ERROR == ret)
          {
            TBSYS_LOG(INFO, "unlink file failed, block: %u not is exist, fileid: %"PRI64_PREFIX"u, action: %d, ret: %d", block_id, file_id, action, ret);
          }
          else
          {
            try_add_repair_task(block_id, ret);
            TBSYS_LOG(INFO, "unlink file failed, other error, block: %u, fileid: %"PRI64_PREFIX"u, action: %d, ret: %d", block_id, file_id, action, ret);
          }
        }
        if (is_master)
        {
          if (TFS_SUCCESS == ret && INVALID_FAMILY_ID != family_info.family_id_)
          {
            message->set_server();
            message->set_unlink_flag(unlink_flag); // use unlink type to pass unlink flag
            int32_t result = post_message_to_server(message, pservers);
            ret = (result  == EXIT_POST_MSG_RET_POST_MSG_ERROR) ?  EXIT_SENDMSG_ERROR : TFS_SUCCESS;
          }

          // only one copy, or in a erasure group & error occurs
          if (pservers.size() <= 1U || (TFS_SUCCESS != ret && INVALID_FAMILY_ID != family_info.family_id_))
          {
            if (message->get_lease_id())
              ds_requester_.req_block_write_complete(block_id, message->get_lease_id(), ret, UNLINK_FLAG_YES);
            StatusMessage* reply_msg = NULL;
            char msg[32];
            if (TFS_SUCCESS == ret)
            {
              snprintf(msg, 32, "%"PRI64_PREFIX"d", file_size);
              reply_msg = new StatusMessage(STATUS_MESSAGE_OK, msg);
            }
            else
            {
              snprintf(msg, 32, "unlink file failed, ret: %d", ret);
              reply_msg = new StatusMessage(STATUS_MESSAGE_ERROR, msg);
            }
            ret = message->reply(reply_msg);
            get_data_management().get_lease_manager().remove(lease->lease_id_);
          }
        }
        else
        {
          UnlinkFileResponseMessage* reply_msg = new UnlinkFileResponseMessage();
          reply_msg->set_lease_id(message->get_lease_id_ext());
          reply_msg->set_server(data_server_info_.id_);
          reply_msg->set_block_info(local_info);
          reply_msg->set_status(ret);
          ret = message->reply(reply_msg);
        }

        // hook to be checked on delete or undelete
        if (TFS_SUCCESS == ret)
        {
          if (DELETE == action || UNDELETE == action)
          {
            check_block_.add_check_task(block_id);
          }
        }

        TIMER_END();
        TBSYS_LOG(INFO, "unlink file %s. family_id: %"PRI64_PREFIX"d, blockid: %d, fileid: %" PRI64_PREFIX "u, action: %d, isserver: %s, peer ip: %s, cost time: %" PRI64_PREFIX "d",
            TFS_SUCCESS == ret ? "success" : "fail", family_info.family_id_, block_id, file_id, action, is_master ? "master" : "slave",
            tbsys::CNetUtil::addrToString(peer_id).c_str(), TIMER_DURATION());

        string sub_key = "";
        TFS_SUCCESS == ret ? sub_key = "unlink-success": sub_key = "unlink-failed";
        stat_mgr_.update_entry(tfs_ds_stat_, sub_key, 1);
      }
      return ret;
    }

    int DataService::new_block(NewBlockMessage* message)
    {
      const VUINT32* new_blocks = message->get_new_blocks();

      int ret = data_management_.batch_new_block(new_blocks);
      if (TFS_SUCCESS != ret)
      {
        return message->reply_error_packet(TBSYS_LOG_LEVEL(ERROR), ret,
            "newblock error, ret: %d", ret);
      }

      message->reply(new StatusMessage(STATUS_MESSAGE_OK));
      return TFS_SUCCESS;
    }

    int DataService::remove_block(RemoveBlockMessage* message)
    {
      std::vector<uint32_t> remove_blocks;
      remove_blocks.push_back(message->get());
      uint64_t peer_id = message->get_connection()->getPeerId();

      TBSYS_LOG(DEBUG, "remove block. peer id: %s", tbsys::CNetUtil::addrToString(peer_id).c_str());

      int ret = data_management_.batch_remove_block(&remove_blocks);
      if (TFS_SUCCESS != ret)
      {
        return message->reply_error_packet(TBSYS_LOG_LEVEL(ERROR), ret,
            "removeblock error, ret: %d", ret);
      }

      if (common::REMOVE_BLOCK_RESPONSE_FLAG_YES == message->get_response_flag())
      {
        RemoveBlockResponseMessage* msg = new RemoveBlockResponseMessage();
        msg->set_seqno(message->get_seqno());
        msg->set(message->get());
        message->reply(msg);
      }
      else
      {
        message->reply(new StatusMessage(STATUS_MESSAGE_OK));
      }
      return TFS_SUCCESS;
    }

    int DataService::query_bit_map(ListBitMapMessage* message)
    {
      int32_t list_type = message->get_bitmap_type();

      int32_t bit_map_len = 0, set_count = 0;
      char* tmp_data_buffer = NULL;

      data_management_.query_bit_map(list_type, &tmp_data_buffer, bit_map_len, set_count);

      RespListBitMapMessage* resp_lbm_msg = new RespListBitMapMessage();
      char* packet_data = resp_lbm_msg->alloc_data(bit_map_len);
      if (NULL == packet_data)
      {
        tbsys::gDeleteA(tmp_data_buffer);
        tbsys::gDelete(resp_lbm_msg);
        TBSYS_LOG(ERROR, "query bitmap. allocate memory fail. type: %d", list_type);
        return TFS_ERROR;
      }

      TBSYS_LOG(DEBUG, "query bitmap. type: %d, bitmaplen: %u, setcount: %u", list_type, bit_map_len, set_count);
      memcpy(packet_data, tmp_data_buffer, bit_map_len);
      resp_lbm_msg->set_length(bit_map_len);
      resp_lbm_msg->set_use_count(set_count);
      message->reply(resp_lbm_msg);
      tbsys::gDeleteA(tmp_data_buffer);
      return TFS_SUCCESS;
    }

    int DataService::list_blocks(ListBlockMessage* message)
    {
      int32_t list_type = message->get_block_type();
      VUINT block_ids;
      map <uint32_t, vector<uint32_t> > logic_2_physic_blocks;
      map<uint32_t, BlockInfo*> block_2_info;

      data_management_.query_block_status(list_type, block_ids, logic_2_physic_blocks, block_2_info);

      RespListBlockMessage* resp_lb_msg = new RespListBlockMessage();
      resp_lb_msg->set_blocks(list_type, &block_ids);
      if (list_type & LB_PAIRS)
      {
        resp_lb_msg->set_pairs(list_type, &logic_2_physic_blocks);
      }
      if (list_type & LB_INFOS)
      {
        resp_lb_msg->set_infos(list_type, &block_2_info);
      }

      message->reply(resp_lb_msg);
      return TFS_SUCCESS;
    }

    int DataService::check_blocks(common::BasePacket* packet)
    {
      int32_t ret = (NULL != packet) ? TFS_SUCCESS : TFS_ERROR;
      if (TFS_SUCCESS == ret)
      {
        CheckBlockRequestMessage* message = dynamic_cast<CheckBlockRequestMessage*>(packet);
        CheckBlockResponseMessage* resp_cb_msg = new CheckBlockResponseMessage();
        uint32_t block_id = message->get_block_id();
        if (0 == block_id)  // block_id: 0, check all blocks
        {
          ret = check_block_.check_all_blocks(resp_cb_msg->get_result_ref(),
              message->get_check_flag(), message->get_check_time(),
              message->get_last_check_time());
        }
        else
        {
          if (0 == message->get_check_flag())  // flag: 0, check specific block
          {
            CheckBlockInfo cbi;
            ret = check_block_.check_one_block(block_id, cbi);
            if (TFS_SUCCESS == ret)
            {
              resp_cb_msg->get_result_ref().push_back(cbi);
            }
          }
          else  // flag: !0, repair block
          {
            ret = check_block_.repair_block_info(block_id);
          }
        }

        if (TFS_SUCCESS == ret)
        {
          ret = packet->reply(resp_cb_msg);
        }
        else
        {
          tbsys::gDelete(resp_cb_msg);
          ret = message->reply_error_packet(TBSYS_LOG_LEVEL(ERROR), ret,
              "check block fail, ret: %d", ret);
        }
      }
      return TFS_SUCCESS;
    }

    int DataService::crc_error_cmd(CrcErrorMessage* message)
    {
      CrcCheckFile* check_file_item = new CrcCheckFile();
      check_file_item->block_id_ = message->get_block_id();
      check_file_item->file_id_ = message->get_file_id();
      check_file_item->crc_ = message->get_crc();
      check_file_item->flag_ = message->get_error_flag();
      check_file_item->fail_servers_ = *(message->get_fail_server());

      int ret = block_checker_.add_repair_task(check_file_item);
      TBSYS_LOG(
          INFO,
          "receive crc error cmd, blockid: %u, fileid: %" PRI64_PREFIX "u, crc: %u, flag: %d, failserver size: %zd, ret: %d\n",
          check_file_item->block_id_, check_file_item->file_id_, check_file_item->crc_, check_file_item->flag_,
          check_file_item->fail_servers_.size(), ret);
      message->reply(new StatusMessage(STATUS_MESSAGE_OK));

      return TFS_SUCCESS;
    }

    int DataService::get_block_info(GetBlockInfoMessage *message)
    {
      uint32_t block_id = message->get_block_id();
      BlockInfo* blk = NULL;
      int32_t visit_count = 0;

      int ret = data_management_.get_block_info(block_id, blk, visit_count);
      if (TFS_SUCCESS != ret)
      {
        return message->reply_error_packet(TBSYS_LOG_LEVEL(ERROR), ret,
            "block is not exist, blockid: %u, ret: %d", block_id, ret);
      }

      UpdateBlockInfoMessage* resp_ubi_msg = new UpdateBlockInfoMessage();
      resp_ubi_msg->set_block(blk);
      //serverid has discarded
      resp_ubi_msg->set_server_id(0);
      resp_ubi_msg->set_repair(visit_count);

      message->reply(resp_ubi_msg);
      return TFS_SUCCESS;
    }

    int DataService::get_server_status(GetServerStatusMessage *message)
    {
      int32_t type = message->get_status_type();
      int32_t ret_row = message->get_return_row();

      if (GSS_MAX_VISIT_COUNT == type)
      {
        //get max visit count block
        vector<LogicBlock*> block_vecs;
        data_management_.get_visit_sorted_blockids(block_vecs);
        CarryBlockMessage* resp_cb_msg = new CarryBlockMessage();
        for (int32_t i = 0; i < ret_row && i < static_cast<int32_t>(block_vecs.size()); ++i)
        {
          resp_cb_msg->add_expire_id(block_vecs[i]->get_block_info()->block_id_);
          resp_cb_msg->add_new_id(block_vecs[i]->get_visit_count());
        }

        message->reply(resp_cb_msg);
        return TFS_SUCCESS;
      }
      else if (GSS_BLOCK_FILE_INFO == type)
      {
        uint32_t block_id = ret_row;
        //get block file list
        vector <FileInfo> fileinfos;
        int ret = data_management_.get_block_file_list(block_id, fileinfos);
        if (TFS_SUCCESS != ret)
        {
          return message->reply_error_packet(TBSYS_LOG_LEVEL(ERROR), ret,
              "GSS_BLOCK_FILE_INFO fail, blockid: %u, ret: %d", block_id, ret);
        }

        BlockFileInfoMessage* resp_bfi_msg = new BlockFileInfoMessage();
        FILE_INFO_LIST* v = resp_bfi_msg->get_fileinfo_list();
        for (uint32_t i = 0; i < fileinfos.size(); ++i)
        {
          v->push_back((fileinfos[i]));
        }
        message->reply(resp_bfi_msg);
        return TFS_SUCCESS;
      }
      else if (GSS_BLOCK_RAW_META_INFO == type)
      {
        //get block inode info
        uint32_t block_id = ret_row;
        RawMetaVec meta_vec;
        int ret = data_management_.get_block_meta_info(block_id, meta_vec);
        if (TFS_SUCCESS != ret)
        {
          return message->reply_error_packet(TBSYS_LOG_LEVEL(ERROR), ret,
              "GSS_BLOCK_RAW_META_INFO fail, blockid: %u, ret: %d", block_id, ret);
        }

        BlockRawMetaMessage* resp_brm_msg = new BlockRawMetaMessage();
        RawMetaVec* v = resp_brm_msg->get_raw_meta_list();
        v->assign(meta_vec.begin(), meta_vec.end());
        message->reply(resp_brm_msg);
        return TFS_SUCCESS;
      }
      else if (GSS_CLIENT_ACCESS_INFO == type)
      {
        //ret_row as ip
        //uint32_t ip = ret_row;
        TBSYS_LOG(DEBUG, "GSS_CLIENT_ACCESS_INFO: from row: %d, return row: %d", message->get_from_row(),
            message->get_return_row());

        AccessStatInfoMessage* result_message = new AccessStatInfoMessage();
        result_message->set_from_row(message->get_from_row());
        result_message->set_return_row(message->get_return_row());
        result_message->set(acs_.get_stat());

        message->reply(result_message);
        return TFS_SUCCESS;
      }

      return message->reply_error_packet(TBSYS_LOG_LEVEL(ERROR), STATUS_MESSAGE_ERROR,
          "get server status type unsupport: %d", type);
    }

    int32_t DataService::client_command(ClientCmdMessage* message)
    {
      StatusMessage* resp = new StatusMessage(STATUS_MESSAGE_ERROR, "unknown client cmd.");
      int32_t type = message->get_cmd();
      uint64_t from_server_id = message->get_value2();
      do
      {
        // load a lost block
        if (CLIENT_CMD_SET_PARAM == type)
        {
          uint32_t block_id = message->get_value3();
          uint64_t server_id = message->get_value1();
          TBSYS_LOG(DEBUG, "set run param block_id: %u, server: %" PRI64_PREFIX "u, from: %s", block_id, server_id,
              tbsys::CNetUtil::addrToString(from_server_id).c_str());
          // block_id as param type,
          int32_t ret = 0;
          if (AccessControl::ACL_FLAG == block_id)
          {
            // server_id as flag value;
            ret = acl_.set_flag(server_id & 0xFF);
          }
          else if (AccessControl::ACL_IPMASK == block_id)
          {
            // server_id as ip; version as mask
            ret = acl_.insert_ipmask(getip(server_id), message->get_version());
          }
          else if (AccessControl::ACL_IPLIST == block_id)
          {
            ret = acl_.insert_iplist(getip(server_id));
          }
          else if (AccessControl::ACL_CLEAR == block_id)
          {
            ret = acl_.clear();
          }
          else if (AccessControl::ACL_RELOAD == block_id )
          {
            ret = acl_.reload();
          }
          if (ret < 0)
            ret = STATUS_MESSAGE_ERROR;
          else
            ret = STATUS_MESSAGE_OK;
          resp->set_message(ret, "cannot set acl, flag not 0");
        }
        else if (CLIENT_CMD_FORCE_DATASERVER_REPORT == type)
        {
          need_send_blockinfo_[0] = true;
          need_send_blockinfo_[1] = true;
        }
      }
      while (0);
      message->reply(resp);
      return TFS_SUCCESS;
    }

    int DataService::reload_config(ReloadConfigMessage* message)
    {
      int32_t ret = SYSPARAM_DATASERVER.initialize(config_file_, server_index_);
      if (TFS_SUCCESS != ret)
      {
        TBSYS_LOG(ERROR, "reload config failed \n");
      }
      else
      {
        // reply first, or maybe timeout
        ret = message->reply(new StatusMessage(STATUS_MESSAGE_OK));
        if (message->get_switch_cluster_flag())
        {
          ret = init_sync_mirror();
          if (TFS_SUCCESS != ret)
          {
            TBSYS_LOG(ERROR, "reload config failed, init sync mirror fail!\n");
          }
        }
        TBSYS_LOG(INFO, "reload config ret: %d\n", ret);
      }
      return ret;
    }

    int DataService::get_ping_status(StatusMessage* message)
    {
      int ret = TFS_SUCCESS;
      if (STATUS_MESSAGE_PING == message->get_status())
      {
        StatusMessage *statusmessage = new StatusMessage(STATUS_MESSAGE_PING);
        message->reply(statusmessage);
      }
      else
      {
        ret = TFS_ERROR;
      }
      return ret;
    }

    int DataService::reset_block_version(ResetBlockVersionMessage* message)
    {
      uint32_t block_id = message->get_block_id();
      int ret = data_management_.reset_block_version(block_id);
      if (TFS_SUCCESS != ret)
      {
        return message->reply_error_packet(TBSYS_LOG_LEVEL(ERROR), ret,
            "reset block version fail, blockid: %u", block_id);
      }

      message->reply(new StatusMessage(STATUS_MESSAGE_OK));
      return TFS_SUCCESS;
    }

    int DataService::write_raw_data(WriteRawDataMessage* message)
    {
      uint32_t block_id = message->get_block_id();
      int32_t msg_len = message->get_length();
      int32_t data_offset = message->get_offset();
      int32_t new_flag = message->get_new_block();
      const char* data_buffer = message->get_data();
      uint64_t peer_id = message->get_connection()->getPeerId();

      TBSYS_LOG(DEBUG, "writeblockdatafile start, blockid: %u, len: %d, offset: %d, new flag: %d, peer id: %s",
          block_id, msg_len, data_offset, new_flag, tbsys::CNetUtil::addrToString(peer_id).c_str());

      int ret = 0;
      if (new_flag)
      {
        if (NORMAL_DATA == new_flag)
        {
          ret = data_management_.new_single_block(block_id, C_REPL_BLOCK);
        }
        else if (PARITY_DATA == new_flag)
        {
          ret = data_management_.new_single_block(block_id, C_PARITY_BLOCK);
        }
        if (TFS_SUCCESS != ret)
        {
          return message->reply_error_packet(TBSYS_LOG_LEVEL(ERROR), ret,
              "write data batch fail, blockid: %u, ret: %d", block_id, ret);
        }

        //add to m_clonedBlockMap
        task_manager_.add_cloned_block_map(block_id);
      }

      ret = data_management_.write_raw_data(block_id, data_offset, msg_len, data_buffer);
      if (TFS_SUCCESS != ret)
      {
        return message->reply_error_packet(TBSYS_LOG_LEVEL(ERROR), ret,
            "write data batch fail, blockid: %u, ret: %d", block_id, ret);
      }

      message->reply(new StatusMessage(STATUS_MESSAGE_OK));
      return TFS_SUCCESS;
    }

    int DataService::batch_write_info(WriteInfoBatchMessage* message)
    {
      uint32_t block_id = message->get_block_id();
      const BlockInfo* blk = message->get_block_info();
      const RawMetaVec* raw_metas = message->get_raw_meta_list();
      uint64_t peer_id = message->get_connection()->getPeerId();
      int32_t remove_flag = message->get_remove_flag();

      TBSYS_LOG(DEBUG, "write block fileinfo start, blockid: %u, cluster flag: %d, meta size: %d, peer id: %s", block_id,
          message->get_cluster(), static_cast<int32_t>(raw_metas->size()), tbsys::CNetUtil::addrToString(peer_id).c_str());
      int ret = data_management_.batch_write_meta(block_id, blk, raw_metas, remove_flag);
      if (TFS_SUCCESS != ret)
      {
        return message->reply_error_packet(TBSYS_LOG_LEVEL(ERROR), ret,
            "write block fileinfo fail, blockid: %u, ret: %d", block_id, ret);
      }

      //between cluster copy
      if (message->get_cluster())
      {
        ret = ds_requester_.req_update_block_info(block_id);
        if (TFS_SUCCESS != ret)
        {
          return message->reply_error_packet(TBSYS_LOG_LEVEL(ERROR), ret,
              "write block FileInfo fail: update block, blockid: %u, ret: %d", block_id, ret);
        }
      }

      //clear m_clonedBlockMap
      task_manager_.del_cloned_block_map(block_id);

      TBSYS_LOG(INFO, "batch write block fileinfo successful, blockid: %u, peer id: %s",
          block_id, tbsys::CNetUtil::addrToString(peer_id).c_str());
      message->reply(new StatusMessage(STATUS_MESSAGE_OK));
      return TFS_SUCCESS;
    }

    int DataService::write_raw_index(WriteRawIndexMessage* message)
    {
      uint32_t block_id = message->get_block_id();
      RawIndexOp index_op = message->get_index_op();
      RawIndexVec* index_vec = message->get_index_vec();
      int64_t family_id = message->get_family_id();
      uint64_t peer_id = message->get_connection()->getPeerId();

      TBSYS_LOG(DEBUG, "write raw index start, blockid: %u, index op: %d, index count: %zd, peer id: %s",
          block_id, (int)index_op, index_vec->size(), tbsys::CNetUtil::addrToString(peer_id).c_str());

      int ret = data_management_.write_raw_index(block_id, family_id, index_op, index_vec);
      if (TFS_SUCCESS != ret)
      {
        message->reply_error_packet(TBSYS_LOG_LEVEL(ERROR), ret,
            "write raw index fail, blockid: %u, ret: %d", block_id, ret);
      }
      else
      {
        message->reply(new StatusMessage(STATUS_MESSAGE_OK));
        TBSYS_LOG(INFO, "write raw index successful, blockid: %u, index op: %d, index count: %zd, peer id: %s",
          block_id, index_op, index_vec->size(), tbsys::CNetUtil::addrToString(peer_id).c_str());
      }

      if (TFS_SUCCESS == ret && index_vec->size() > 0)
      {
        task_manager_.del_cloned_block_map(block_id);
      }

      return TFS_SUCCESS;
    }

    int DataService::read_raw_index(ReadRawIndexMessage* message)
    {
      uint32_t block_id = message->get_block_id();
      uint32_t index_id = message->get_index_id();
      RawIndexOp index_op = message->get_index_op();
      uint64_t peer_id = message->get_connection()->getPeerId();
      int32_t size = 0;
      char* tmp_buf = NULL;

      TBSYS_LOG(DEBUG, "read raw index start, blockid: %u, index id: %u, index_op: %d, peer id: %s",
          block_id, index_id, (int)index_op, tbsys::CNetUtil::addrToString(peer_id).c_str());

      RespReadRawIndexMessage* resp_msg = new RespReadRawIndexMessage();
      int ret = data_management_.read_raw_index(block_id, index_op, index_id, tmp_buf, size);
      if (TFS_SUCCESS != ret)
      {
        resp_msg->set_length(ret);
        message->reply(resp_msg);
      }
      else
      {
        char* packet_data = resp_msg->alloc_data(size);
        if (NULL == packet_data)
        {
          TBSYS_LOG(ERROR, "alloc data failed, blockid: %u, real len: %d", block_id, size);
          ret = TFS_ERROR;
          message->reply_error_packet(TBSYS_LOG_LEVEL(ERROR), ret, "dataserver memory insufficient");
        }
        else
        {
          resp_msg->set_length(size);
          memcpy(packet_data, tmp_buf, size);
          message->reply(resp_msg);
          TBSYS_LOG(INFO, "read raw index successful, blockid: %u, index id: %u, index_op: %d, peer id: %s",
            block_id, index_id, (int)index_op, tbsys::CNetUtil::addrToString(peer_id).c_str());
        }
      }
      tbsys::gDelete(tmp_buf);

      return TFS_SUCCESS;
    }

    int DataService::get_dataserver_information(common::BasePacket* packet)
    {
      int32_t iret = NULL != packet ? TFS_SUCCESS : TFS_ERROR;
      if (TFS_SUCCESS == iret)
      {
        GetDataServerInformationMessage* message = dynamic_cast<GetDataServerInformationMessage*>(packet);
        GetDataServerInformationResponseMessage* reply_msg = new GetDataServerInformationResponseMessage();
        int32_t flag = message->get_flag();
        char* tmp_data_buffer = NULL;
        int32_t bit_map_len = 0;
        int32_t& set_count = reply_msg->get_bit_map_element_count();
        data_management_.query_bit_map(flag, &tmp_data_buffer, bit_map_len, set_count);
        char* data = reply_msg->alloc_data(bit_map_len);
        if (NULL == data)
        {
          tbsys::gDeleteA(tmp_data_buffer);
          tbsys::gDelete(reply_msg);
          TBSYS_LOG(ERROR, "query bitmap. allocate memory fail. type: %d", flag);
          iret = TFS_ERROR;
        }
        else
        {
          TBSYS_LOG(DEBUG, "query bitmap. type: %d, bitmaplen: %u, setcount: %u", flag, bit_map_len, set_count);
          memcpy(data, tmp_data_buffer, bit_map_len);
          tbsys::gDeleteA(tmp_data_buffer);

          SuperBlock block;
          memset(&block, 0, sizeof(block));
          iret = BlockFileManager::get_instance()->query_super_block(block);
          if (TFS_SUCCESS == iret)
          {
            reply_msg->set_super_block(block);
            reply_msg->set_dataserver_stat_info(data_server_info_);
            iret = packet->reply(reply_msg);
          }
          else
          {
            tbsys::gDelete(reply_msg);
            TBSYS_LOG(ERROR, "query super block information fail. ret: %d", iret);
          }
        }
      }
      return iret;
    }


    // send blockinfos to dataserver
    int DataService::send_blocks_to_ns(int8_t& heart_interval, const int32_t who, const int64_t timeout)
    {
      int iret = 0 != who && 1 != who ? SEND_BLOCK_TO_NS_PARAMETER_ERROR : TFS_SUCCESS;
      if (TFS_SUCCESS == iret)
      {
        // ns who is not set
        iret = !set_flag_[who] ? SEND_BLOCK_TO_NS_NS_FLAG_NOT_SET : TFS_SUCCESS;
      }
      if (TFS_SUCCESS == iret)
      {
        bool reset_need_send_blockinfo_flag = data_management_.get_all_logic_block_size() <= 0;
        SetDataserverMessage req_sds_msg;
        req_sds_msg.set_ds(&data_server_info_);
        if (need_send_blockinfo_[who])
        {
          reset_need_send_blockinfo_flag = true;
          req_sds_msg.set_has_block(HAS_BLOCK_FLAG_YES);

          list<LogicBlock*> logic_block_list;
          data_management_.get_all_logic_block(logic_block_list);
          for (list<LogicBlock*>::iterator lit = logic_block_list.begin(); lit != logic_block_list.end(); ++lit)
          {
            TBSYS_LOG(DEBUG, "send block to ns: %d, blockid: %u\n", who, (*lit)->get_logic_block_id());
            req_sds_msg.add_block((*lit)->get_block_info());
          }
        }
        tbnet::Packet* message = NULL;
        NewClient* client = NewClientManager::get_instance().create_client();
        iret = NULL != client ? TFS_SUCCESS : SEND_BLOCK_TO_NS_CREATE_NETWORK_CLIENT_ERROR;
        if (TFS_SUCCESS == iret)
        {
          iret = send_msg_to_server(hb_ip_port_[who], client, &req_sds_msg, message, timeout);
          if (TFS_SUCCESS != iret)
          {
            TBSYS_LOG(INFO, "send_msg_to_server: %s failed, ret: %d", tbsys::CNetUtil::addrToString(hb_ip_port_[who]).c_str(), iret);
            iret = SEND_BLOCK_TO_NS_NETWORK_ERROR;
          }
          else
          {
            iret = RESP_HEART_MESSAGE == message->getPCode() ? TFS_SUCCESS : SEND_BLOCK_TO_NS_NETWORK_ERROR;
            if (TFS_SUCCESS == iret)
            {
              RespHeartMessage* resp_hb_msg = dynamic_cast<RespHeartMessage*>(message);
              heart_interval = resp_hb_msg->get_heart_interval();
              int32_t status = resp_hb_msg->get_status();
              if (HEART_MESSAGE_FAILED != status)
              {
                if (reset_need_send_blockinfo_flag
                    && need_send_blockinfo_[who])
                {
                  need_send_blockinfo_[who] = false;
                }
                if (HEART_NEED_SEND_BLOCK_INFO == status)
                {
                  TBSYS_LOG(DEBUG, "nameserver %d ask for send block\n", who + 1);
                  need_send_blockinfo_[who] = true;
                }
                else if (HEART_EXP_BLOCK_ID == status)
                {
                  TBSYS_LOG(INFO, "nameserver %d ask for expire block\n", who + 1);
                  data_management_.add_new_expire_block(resp_hb_msg->get_expire_blocks(), NULL, resp_hb_msg->get_new_blocks());
                }
                else if (HEART_REPORT_BLOCK_SERVER_OBJECT_NOT_FOUND == status)
                {
                  TBSYS_LOG(INFO, "nameserver %d ask for expire block\n", who + 1);
                  need_send_blockinfo_[who] = false;
                }
                else if (HEART_REPORT_UPDATE_RELATION_ERROR == status)
                {
                  need_send_blockinfo_[who] = true;
                }
              }
            }
          }
        }
        NewClientManager::get_instance().destroy_client(client);
      }
      return iret;
   }

   int DataService::send_blocks_to_ns(common::BasePacket* packet)
   {
     int32_t iret = NULL != packet ? TFS_SUCCESS : TFS_ERROR;
     if (TFS_SUCCESS == iret)
     {
       iret = packet->getPCode() == REQ_CALL_DS_REPORT_BLOCK_MESSAGE ? TFS_SUCCESS : TFS_ERROR;
     }
     if (TFS_SUCCESS == iret)
     {
       CallDsReportBlockRequestMessage* msg = dynamic_cast<CallDsReportBlockRequestMessage*>(packet);
       ReportBlocksToNsRequestMessage req_msg;
       ReportBlocksToNsRequestMessage clear_req_msg;
       ReportBlocksToNsRequestMessage delete_req_msg;
       req_msg.set_server(data_server_info_.id_);
       if (REPORT_BLOCK_NORMAL == msg->get_flag())
       {
         data_management_.get_all_block_info(req_msg.get_blocks());
       }
       else
       {
         data_management_.get_all_block_info(req_msg.get_blocks_ext());
       }
       req_msg.set_flag(msg->get_flag());
       TBSYS_LOG(INFO, "report block to ns, blocks size: %zd, ext size: %zd",
           req_msg.get_blocks().size(), req_msg.get_blocks_ext().size());

       NewClient* client = NewClientManager::get_instance().create_client();
       tbnet::Packet* message = NULL;
       iret = send_msg_to_server(msg->get_server(), client, &req_msg, message);
       if (TFS_SUCCESS == iret)
       {
         iret = message->getPCode() == RSP_REPORT_BLOCKS_TO_NS_MESSAGE ? TFS_SUCCESS : TFS_ERROR;
         if (TFS_SUCCESS == iret)
         {
           ReportBlocksToNsResponseMessage* msg = dynamic_cast<ReportBlocksToNsResponseMessage*>(message);
           TBSYS_LOG(INFO, "nameserver %s ask for expire block\n",
               tbsys::CNetUtil::addrToString(msg->get_server()).c_str());
           data_management_.expire_blocks(msg->get_blocks(),
               clear_req_msg.get_blocks_ext(), delete_req_msg.get_blocks_ext());
         }
       }
       NewClientManager::get_instance().destroy_client(client);

       if (TFS_SUCCESS ==  iret)
       {
         if (clear_req_msg.get_blocks_ext().size() != 0)
         {
           clear_req_msg.set_flag(msg->get_flag());
           clear_req_msg.set_server(data_server_info_.id_);
           clear_req_msg.set_type(REPORT_BLOCK_TYPE_PART);
           NewClient* client = NewClientManager::get_instance().create_client();
           tbnet::Packet* message = NULL;
           send_msg_to_server(msg->get_server(), client, &req_msg, message);
           NewClientManager::get_instance().destroy_client(client);
         }

         if (delete_req_msg.get_blocks_ext().size() != 0)
         {
           delete_req_msg.set_flag(msg->get_flag());
           delete_req_msg.set_server(data_server_info_.id_);
           delete_req_msg.set_type(REPORT_BLOCK_TYPE_RELIEVE);
           NewClient* client = NewClientManager::get_instance().create_client();
           tbnet::Packet* message = NULL;
           send_msg_to_server(msg->get_server(), client, &req_msg, message);
           NewClientManager::get_instance().destroy_client(client);
         }
       }
     }

     return iret;
   }

    void DataService::do_stat(const uint64_t peer_id,
        const int32_t visit_file_size, const int32_t real_len, const int32_t offset, const int32_t mode)
    {
      count_mutex_.lock();
      if (AccessStat::READ_BYTES == mode)
      {
        data_server_info_.total_tp_.read_byte_ += real_len;
        acs_.incr(peer_id, AccessStat::READ_BYTES, real_len);
        if (0 == offset)
        {
          data_server_info_.total_tp_.read_file_count_++;
          visit_stat_.stat_visit_count(visit_file_size);
          acs_.incr(peer_id, AccessStat::READ_COUNT, 1);
        }
      }
      else if (AccessStat::WRITE_BYTES == mode)
      {
        data_server_info_.total_tp_.write_byte_ += visit_file_size;
        data_server_info_.total_tp_.write_file_count_++;
        acs_.incr(peer_id, AccessStat::WRITE_BYTES, visit_file_size);
        acs_.incr(peer_id, AccessStat::WRITE_COUNT, 1);
      }
      else
      {
        data_server_info_.total_tp_.read_byte_ += real_len;
        if (0 == offset)
        {
          data_server_info_.total_tp_.read_file_count_++;
        }
      }
      count_mutex_.unlock();
      return;
    }

    void DataService::try_add_repair_task(const uint32_t block_id, const int ret)
    {
      TBSYS_LOG(INFO, "add repair task, blockid: %u, ret: %d", block_id, ret);
      if (ret == -EIO)
      {
        CrcCheckFile* check_file_item = new CrcCheckFile(block_id, CHECK_BLOCK_EIO);
        block_checker_.add_repair_task(check_file_item);
      }
      return;
    }

    int DataService::init_log_file(tbsys::CLogger& logger, const string& log_file)
    {
      int32_t iret = log_file.empty() ? TFS_ERROR : TFS_SUCCESS;
      if (TFS_SUCCESS == iret)
      {
        logger.setLogLevel(get_log_file_level());
        if (access(log_file.c_str(), R_OK) == 0)
        {
          char old_log_file[256];
          sprintf(old_log_file, "%s.%s", log_file.c_str(), Func::time_to_str(time(NULL), 1).c_str());
          rename(log_file.c_str(), old_log_file);
        }
        logger.setFileName(log_file.c_str(), true);
        logger.setMaxFileSize(get_log_file_size());
        logger.setMaxFileIndex(get_log_file_count());
      }
      return iret;
    }

    void DataService::HeartBeatThreadHelper::run()
    {
      service_.run_heart(who_);
    }

    void DataService::DoCheckThreadHelper::run()
    {
      service_.run_check();
    }

    void DataService::TaskThreadHelper::run()
    {
      service_.task_manager_.run_task();
    }

    int ds_async_callback(common::NewClient* client)
    {
      DataService* service = dynamic_cast<DataService*>(BaseMain::instance());
      int32_t iret = NULL != service ? TFS_SUCCESS : TFS_ERROR;
      if (TFS_SUCCESS == iret)
      {
        iret = service->callback(client);
      }
      return iret;
    }

  }
}
