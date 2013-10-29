/*
 * (C) 2007-2010 Alibaba Group Holding Limited.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 *
 * Version: $Id: internal.h 868 2011-09-29 05:07:38Z duanfei@taobao.com $
 *
 * Authors:
 *   duolong <duolong@taobao.com>
 *      - initial release
 *
 */
#ifndef TFS_COMMON_INTERNAL_H_
#define TFS_COMMON_INTERNAL_H_

#include <string>
#include <map>
#include <vector>
#include <list>
#include <set>
#include <ext/hash_map>
#include <string.h>
#include <stdint.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include <assert.h>
#include <limits.h>

#include "tbnet.h"

#include "define.h"

#define GET_DATA_MEMBER_NUM(x) ((x >> 24) & 0xFF)
#define GET_CHECK_MEMBER_NUM(x) ((x >> 16) & 0xFF)
#define GET_MARSHALLING_TYPE(x) ((x >> 8) & 0xFF)
#define GET_MASTER_INDEX(x) (x & 0xFF)

#define SET_DATA_MEMBER_NUM(x,y) (x = (x & 0x00FFFFFF) | ((y << 24) & 0xFF000000))
#define SET_CHECK_MEMBER_NUM(x,y) (x = (x & 0xFF00FFFF) | ((y << 16) & 0xFF0000))
#define SET_MARSHALLING_TYPE(x,y) (x = (x & 0xFFFF00FF) | ((y << 8) & 0xFF00))
#define SET_MASTER_INDEX(x,y) (x = (x & 0xFFFFFF00) | (y & 0xFF))

#define CHECK_DATA_MEMBER_NUM(x) (x > 0 && x <= MAX_DATA_MEMBER_NUM)
#define CHECK_CHECK_MEMBER_NUM(x) (x > 0 && x <= MAX_CHECK_MEMBER_NUM)
#define CHECK_MEMBER_NUM(x) (x > 0 && x <= MAX_MARSHALLING_NUM)
#define CHECK_MEMBER_NUM_V2(x, y) ( x > 0 && x <= MAX_DATA_MEMBER_NUM \
    && y > 0 && y <= MAX_CHECK_MEMBER_NUM && (x+y) > 0 && (x+y) <= MAX_MARSHALLING_NUM)

//#define TFS_GTEST

#if __WORDSIZE == 32
namespace __gnu_cxx
{
  template<> struct hash<uint64_t>
  {
    uint64_t operator()(uint64_t __x) const
    {
      return __x;
    }
  };

  template<> struct hash<int64_t>
  {
    int64_t operator()(int64_t __x) const
    {
      return __x;
    }
  };
}
#endif

namespace tfs
{
  namespace common
  {
    //typedef base type
    typedef std::vector<int64_t> VINT64;
    typedef std::vector<uint64_t> VUINT64;
    typedef std::vector<int32_t> VINT32;
    typedef std::vector<uint32_t> VUINT32;
    typedef std::vector<int32_t> VINT;
    typedef std::vector<uint32_t> VUINT;
    typedef std::vector<int16_t> VINT16;
    typedef std::vector<uint16_t> VUINT16;
    typedef std::vector<int8_t> VINT8;
    typedef std::vector<uint8_t> VUINT8;
    typedef std::vector<std::string> VSTRING;

    typedef std::map<std::string, std::string> STRING_MAP; // string => string
    typedef STRING_MAP::iterator STRING_MAP_ITER;

    typedef __gnu_cxx ::hash_map<uint32_t, VINT64> INT_VINT64_MAP; // int => vector<int64>
    typedef INT_VINT64_MAP::iterator INT_VINT64_MAP_ITER;
    typedef __gnu_cxx ::hash_map<uint64_t, VINT, __gnu_cxx ::hash<int> > INT64_VINT_MAP; // int64 => vector<int>
    typedef INT64_VINT_MAP::iterator INT64_VINT_MAP_ITER;
    typedef __gnu_cxx ::hash_map<uint32_t, uint64_t> INT_INT64_MAP; // int => int64
    typedef INT_INT64_MAP::iterator INT_INT64_MAP_ITER;
    typedef __gnu_cxx ::hash_map<uint64_t, uint32_t, __gnu_cxx ::hash<int> > INT64_INT_MAP; // int64 => int
    typedef INT64_INT_MAP::iterator INT64_INT_MAP_ITER;

    typedef __gnu_cxx ::hash_map<uint32_t, uint32_t> INT_MAP;
    typedef INT_MAP::iterator INT_MAP_ITER;

    // base constant
    static const int8_t INT8_SIZE = 1;
    static const int8_t INT16_SIZE = 2;
    static const int8_t INT_SIZE = 4;
    static const int8_t INT64_SIZE = 8;

    static const int32_t MAX_PATH_LENGTH = 256;
    static const int32_t MAX_ADDRESS_LENGTH = 64;
    static const int64_t TFS_MALLOC_MAX_SIZE = 0x00A00000;//10M
    static const int64_t MAX_CMD_SIZE = 1024;

    static const uint32_t INVALID_LEASE_ID = 0;
    static const uint32_t INVALID_BLOCK_ID = 0;
    static const int64_t  INVALID_FAMILY_ID = 0;
    static const uint32_t INVALID_RACK_ID = 0;
    static const uint32_t INVALID_SERVER_ID = 0;
    static const int32_t  INVALID_VERSION = -1;

    static const int32_t SEGMENT_HEAD_RESERVE_SIZE = 64;

    static const int32_t MAX_DEV_NAME_LEN = 64;
    static const int32_t MAX_READ_SIZE = 1048576;
    static const int32_t MAX_READ_SIZE_KV = 1 << 21; //2M

    static const int MAX_FILE_FD = INT_MAX;
    static const int MAX_OPEN_FD_COUNT = MAX_FILE_FD - 1;

    static const int32_t SPEC_LEN = 32;
    static const int32_t MAX_RESPONSE_TIME = 30000;

    static const int32_t ADMIN_WARN_DEAD_COUNT = 1;

    static const int64_t DEFAULT_NETWORK_CALL_TIMEOUT  = 3000;//3s
    static const int64_t DEFAULT_TAIR_TIMEOUT  = 15;//15ms

    static const int64_t MAX_META_SIZE = 1 << 21; // 2M
    static const int64_t INVALID_FILE_SIZE = -1;

    // client config
    static const int32_t DEFAULT_CLIENT_RETRY_COUNT = 3;
    static const int32_t DEFAULT_META_RETRY_COUNT = 3;
    static const uint32_t DEFAULT_UPDATE_KMT_INTERVAL_COUNT = 100;
    static const uint32_t DEFAULT_UPDATE_KMT_FAIL_COUNT = 10;
    // unit ms
    static const int64_t DEFAULT_STAT_INTERNAL = 60000; // 1min
    static const int64_t DEFAULT_GC_INTERNAL = 43200000; // 12h

    static const int64_t MIN_GC_EXPIRED_TIME = 21600000; // 6h
    static const int64_t MAX_SEGMENT_SIZE = 1 << 21; // 2M
    static const int64_t MAX_BATCH_DATA_LENGTH = 1 << 23; // 8M
    static const int64_t MAX_BATCH_COUNT = 16;

    static const int32_t MAX_DEV_TAG_LEN = 8;

    static const int32_t DEFAULT_HEART_INTERVAL = 2;//2s

    static const int32_t MAX_REPLICATION_NUM = 8;

    static const int32_t REPORT_BLOCK_NORMAL = 0;
    static const int32_t REPORT_BLOCK_EXT = 1;

    static const int32_t MAX_MARSHALLING_NUM = 12;
    static const int32_t MAX_DATA_MEMBER_NUM = 8;
    static const int32_t MAX_CHECK_MEMBER_NUM = 4;
    static const int32_t MAX_MARSHALLING_BLOCK_SIZE_LIMIT = 128 * 1024 * 1024;

    static const int32_t USE_INDEX_FLAG_MASK = 0x08000000; // 27th bit
    static const int32_t FILE_UNLINK_MASK = 0x70000000;    // 28-30th bit
    static const int32_t FILE_UNLINK_OFFSET = 28;
    static const int32_t FILE_SIZE_MASK = 0x07FFFFFF;      // 0~26th bit

    enum VersionStep
    {
      VERSION_INC_STEP_NONE = 0,
      VERSION_INC_STEP_DEFAULT = 1,
      VERSION_INC_STEP_REPLICATE = 2
    };

    enum OplogFlag
    {
      OPLOG_INSERT = 1,
      OPLOG_UPDATE,
      OPLOG_REMOVE,
      OPLOG_RENAME,
      OPLOG_RELIEVE_RELATION
    };

    enum DataServerLiveStatus
    {
      DATASERVER_STATUS_ALIVE = 0x00,
      DATASERVER_STATUS_DEAD
    };

    //blockinfo message
    enum UpdateBlockType
    {
      UPDATE_BLOCK_NORMAL = 100,
      UPDATE_BLOCK_REPAIR,
      UPDATE_BLOCK_MISSING
    };

    enum ListBlockType
    {
      LB_BLOCK = 1,
      LB_PAIRS = 2,
      LB_INFOS = 4
    };

    enum ListBitmapType
    {
      NORMAL_BIT_MAP,
      ERROR_BIT_MAP
    };

    enum UnlinkFlag
    {
      UNLINK_FLAG_NO = 0x0,
      UNLINK_FLAG_YES
    };

    enum HasBlockFlag
    {
      HAS_BLOCK_FLAG_NO = 0x0,
      HAS_BLOCK_FLAG_YES
    };

    enum GetServerStatusType
    {
      GSS_MAX_VISIT_COUNT,
      GSS_BLOCK_FILE_INFO,
      GSS_BLOCK_RAW_META_INFO,
      GSS_CLIENT_ACCESS_INFO
    };

    enum CheckDsBlockType
    {
      CRC_DS_PATIAL_ERROR = 0,
      CRC_DS_ALL_ERROR,
      CHECK_BLOCK_EIO
    };

    // write data info
    enum ServerRole
    {
      Master_Server_Role = 0,
      Slave_Server_Role
    };

    enum ReplicateBlockMoveFlag
    {
      REPLICATE_BLOCK_MOVE_FLAG_NO = 0x00,
      REPLICATE_BLOCK_MOVE_FLAG_YES
    };

    enum WriteCompleteStatus
    {
      WRITE_COMPLETE_STATUS_YES = 0x00,
      WRITE_COMPLETE_STATUS_NO
    };

    // close write file msg
    enum CloseFileServer
    {
      CLOSE_FILE_MASTER = 100,
      CLOSE_FILE_SLAVER
    };

    enum SsmType
    {
      SSM_BLOCK = 1,
      SSM_SERVER = 2,
      SSM_WBLIST = 4
    };

    enum ServerStatus
    {
      IS_SERVER = 1
    };

    enum ReadDataVersion
    {
      READ_VERSION_2 = 2,
      READ_VERSION_3 = 3
    };

    enum BaseFsType
    {
      NO_INIT = 0,
      EXT4,
      EXT3_FULL,
      EXT3_FTRUN
    };

    enum ClientCmd
    {
      CLIENT_CMD_EXPBLK = 1,
      CLIENT_CMD_LOADBLK,
      CLIENT_CMD_COMPACT,
      CLIENT_CMD_IMMEDIATELY_REPL,
      CLIENT_CMD_REPAIR_GROUP,
      CLIENT_CMD_SET_PARAM,
      CLIENT_CMD_UNLOADBLK,
      CLIENT_CMD_FORCE_DATASERVER_REPORT,
      CLIENT_CMD_ROTATE_LOG,
      CLIENT_CMD_GET_BALANCE_PERCENT,
      CLIENT_CMD_SET_BALANCE_PERCENT,
      CLIENT_CMD_CLEAR_SYSTEM_TABLE
    };

    enum PlanInterruptFlag
    {
      INTERRUPT_NONE= 0x00,
      INTERRUPT_ALL = 0x01
    };

    enum PlanType
    {
      PLAN_TYPE_REPLICATE = 0,
      PLAN_TYPE_MOVE,
      PLAN_TYPE_COMPACT,
      PLAN_TYPE_EC_REINSTATE,
      PLAN_TYPE_EC_DISSOLVE,
      PLAN_TYPE_EC_MARSHALLING
    };

    enum PlanStatus
    {
      PLAN_STATUS_NONE = 0x00,
      PLAN_STATUS_BEGIN,
      PLAN_STATUS_END,
      PLAN_STATUS_FAILURE,
      PLAN_STATUS_TIMEOUT
    };

    enum PlanPriority
    {
      PLAN_PRIORITY_NONE = -1,
      PLAN_PRIORITY_NORMAL = 0,
      PLAN_PRIORITY_EMERGENCY = 1
    };

    enum PlanRunFlag
    {
      PLAN_RUN_FLAG_NONE = 0,
      PLAN_RUN_FLAG_REPLICATE = 1,
      PLAN_RUN_FLAG_MOVE = 1 << 1,
      PLAN_RUN_FLAG_COMPACT = 1 << 2,
      PLAN_RUN_FALG_MARSHALLING = 1 << 3,
      PLAN_RUN_FALG_REINSTATE = 1 << 4,
      PLAN_RUN_FALG_DISSOLVE  = 1 << 5
    };

    enum DeleteExcessBackupStrategy
    {
      DELETE_EXCESS_BACKUP_STRATEGY_NORMAL = 1,
      DELETE_EXCESS_BACKUP_STRATEGY_BY_GROUP =  1 << 1
    };

    enum SSMType
    {
      SSM_TYPE_BLOCK = 0x01,
      SSM_TYPE_SERVER = 0x02
    };
    enum SSMChildBlockType
    {
      SSM_CHILD_BLOCK_TYPE_INFO   = 0x01,
      SSM_CHILD_BLOCK_TYPE_SERVER = 0x02,
      SSM_CHILD_BLOCK_TYPE_FULL = 0x04
    };

    enum SSMChildServerType
    {
      SSM_CHILD_SERVER_TYPE_ALL = 0x01,
      SSM_CHILD_SERVER_TYPE_HOLD = 0x02,
      SSM_CHILD_SERVER_TYPE_WRITABLE = 0x04,
      SSM_CHILD_SERVER_TYPE_MASTER = 0x08,
      SSM_CHILD_SERVER_TYPE_INFO = 0x10
    };

    enum SSMPacketType
    {
      SSM_PACKET_TYPE_REQUEST = 0x01,
      SSM_PACKET_TYPE_REPLAY  = 0x02
    };
    enum SSMScanEndFlag
    {
      SSM_SCAN_END_FLAG_YES = 0x01,
      SSM_SCAN_END_FLAG_NO  = 0x02
    };
    enum SSMScanCutoverFlag
    {
      SSM_SCAN_CUTOVER_FLAG_YES = 0x01,
      SSM_SCAN_CUTOVER_FLAG_NO  = 0x02
    };

    enum TfsFileType
    {
      INVALID_TFS_FILE_TYPE = 0,
      SMALL_TFS_FILE_TYPE,
      LARGE_TFS_FILE_TYPE
    };

    enum BlkType
    {
      BLOCK_TYPE_DATA_BLOCK = 0,
      BLOCK_TYPE_CHECK_BLOCK
    };

    struct SSMScanParameter
    {
      int deserialize(const char* data, const int64_t data_len, int64_t& pos);
      int serialize(char* data, const int64_t data_len, int64_t& pos) const;
      int64_t length() const;
      mutable tbnet::DataBuffer data_;
      int64_t addition_param1_;
      int64_t addition_param2_;
      uint32_t  start_next_position_;//16~32 bit: start, 0~15 bit: next
      uint32_t  should_actual_count_;//16~32 bit: should_count 0~15: actual_count
      int16_t  child_type_;
      int8_t   type_;
      int8_t   end_flag_;
    };
    // common data structure
#pragma pack(4)
    struct FileInfo
    {
      int deserialize(const char* data, const int64_t data_len, int64_t& pos);
      int serialize(char* data, const int64_t data_len, int64_t& pos) const;
      int64_t length() const;
      uint64_t id_; // file id
      int32_t offset_; // offset in block file
      int32_t size_; // file size
      int32_t usize_; // hold space
      int32_t modify_time_; // modify time
      int32_t create_time_; // create time
      int32_t flag_; // deleta flag
      uint32_t crc_; // crc value
    };

    struct BlockInfo
    {
      int deserialize(const char* data, const int64_t data_len, int64_t& pos);
      int serialize(char* data, const int64_t data_len, int64_t& pos) const;
      int64_t length() const;
      uint32_t block_id_;
      int32_t version_;
      int32_t file_count_;
      int32_t size_;
      int32_t del_file_count_;
      int32_t del_size_;
      uint32_t seq_no_;

      BlockInfo(const BlockInfo& info)
      {
        block_id_ = info.block_id_;
        version_  = info.version_;
        file_count_ = info.file_count_;
        size_ = info.size_;
        del_file_count_ = info.del_file_count_;
        del_size_ = info.del_size_;
        seq_no_ = info.seq_no_;
      }

      BlockInfo& operator=(const BlockInfo& info)
      {
        block_id_ = info.block_id_;
        version_  = info.version_;
        file_count_ = info.file_count_;
        size_ = info.size_;
        del_file_count_ = info.del_file_count_;
        del_size_ = info.del_size_;
        seq_no_ = info.seq_no_;
        return *this;
      }

      BlockInfo()
      {
        memset(this, 0, sizeof(BlockInfo));
      }

      bool operator < (const BlockInfo& rhs) const
      {
        return block_id_ < rhs.block_id_;
      }

      inline bool operator==(const BlockInfo& rhs) const
      {
        return block_id_ == rhs.block_id_ && version_ == rhs.version_ && file_count_ == rhs.file_count_ && size_
          == rhs.size_ && del_file_count_ == rhs.del_file_count_ && del_size_ == rhs.del_size_ && seq_no_
          == rhs.seq_no_;
      }
    };

    struct BlockInfoExt
    {
      BlockInfo block_info_;
      int64_t family_id_;
      int deserialize(const char* data, const int64_t data_len, int64_t& pos);
      int serialize(char* data, const int64_t data_len, int64_t& pos) const;
      int64_t length() const;

      BlockInfoExt(const BlockInfoExt& info)
      {
        block_info_ = info.block_info_;
        family_id_ = info.family_id_;
      }

      BlockInfoExt& operator=(const BlockInfoExt& info)
      {
        block_info_ = info.block_info_;
        family_id_ = info.family_id_;
        return *this;
      }

      BlockInfoExt()
      {
        memset(this, 0, sizeof(BlockInfoExt));
      }

      bool operator < (const BlockInfoExt& rhs) const
      {
        return block_info_.block_id_ < rhs.block_info_.block_id_;
      }

   };

    struct RawMeta
    {
      public:
        RawMeta()
        {
          init();
        }

        void init()
        {
          fileid_ = 0;
          location_.inner_offset_ = 0;
          location_.size_ = 0;
        }

        RawMeta(const uint64_t file_id, const int32_t in_offset, const int32_t file_size)
        {
          fileid_ = file_id;
          location_.inner_offset_ = in_offset;
          location_.size_ = file_size;
        }

        RawMeta(const RawMeta& raw_meta)
        {
          memcpy(this, &raw_meta, sizeof(RawMeta));
        }

        int deserialize(const char* data, const int64_t data_len, int64_t& pos);
        int serialize(char* data, const int64_t data_len, int64_t& pos) const;
        int64_t length() const;
        uint64_t get_key() const
        {
          return fileid_;
        }

        void set_key(const uint64_t key)
        {
          fileid_ = key;
        }

        uint64_t get_file_id() const
        {
          return fileid_;
        }

        void set_file_id(const uint64_t file_id)
        {
          fileid_ = file_id;
        }

        int32_t get_offset() const
        {
          return location_.inner_offset_;
        }

        void set_offset(const int32_t offset)
        {
          location_.inner_offset_ = offset;
        }

        int32_t get_size() const
        {
          return location_.size_ & FILE_SIZE_MASK;
        }

        void set_size(const int32_t file_size)
        {
          location_.size_ = (location_.size_ & ~FILE_SIZE_MASK) | (file_size & FILE_SIZE_MASK);
        }

        int32_t get_unlink_flag() const
        {
          return (location_.size_ & FILE_UNLINK_MASK) >> FILE_UNLINK_OFFSET;
        }

        void set_unlink_flag(const int32_t flag)
        {
          location_.size_ = (location_.size_ & ~FILE_UNLINK_MASK) | ((flag << FILE_UNLINK_OFFSET) & FILE_UNLINK_MASK);
          set_use_index_flag();
        }

        bool use_index_flag() const
        {
          return location_.size_ & USE_INDEX_FLAG_MASK;
        }

        void set_use_index_flag()
        {
          location_.size_ |= USE_INDEX_FLAG_MASK;
        }

        bool operator==(const RawMeta& rhs) const
        {
          /*
          return fileid_ == rhs.fileid_ && location_.inner_offset_ == rhs.location_.inner_offset_ && location_.size_
            == rhs.location_.size_;
          */
          return fileid_ == rhs.fileid_ && location_.inner_offset_ == rhs.location_.inner_offset_ &&
            get_size() == rhs.get_size();
        }

      private:
        uint64_t fileid_;
        struct
        {
          int32_t inner_offset_;
          int32_t size_;
        } location_;
    };

    struct ReplBlock
    {
      int deserialize(const char* data, const int64_t data_len, int64_t& pos);
      int serialize(char* data, const int64_t data_len, int64_t& pos) const;
      int64_t length() const;
      uint32_t block_id_;
      uint64_t source_id_;
      uint64_t destination_id_;
      int32_t start_time_;
      int32_t is_move_;
      int32_t server_count_;
    };

    struct CheckBlockInfo
    {
      int serialize(char* data, const int64_t data_len, int64_t& pos) const;
      int deserialize(const char* data, const int64_t data_len, int64_t& pos);
      int64_t length() const;

      uint32_t block_id_;
      int32_t version_;
      uint32_t file_count_;
      uint32_t total_size_;

      CheckBlockInfo(): block_id_(0), version_(0), file_count_(0), total_size_(0)
      {
      }
    };

    struct Throughput
    {
      int deserialize(const char* data, const int64_t data_len, int64_t& pos);
      int serialize(char* data, const int64_t data_len, int64_t& pos) const;
      int64_t length() const;
      int64_t write_byte_;
      int64_t write_file_count_;
      int64_t read_byte_;
      int64_t read_file_count_;
    };

    //dataserver stat info
    struct DataServerStatInfo
    {
      int deserialize(const char* data, const int64_t data_len, int64_t& pos);
      int serialize(char* data, const int64_t data_len, int64_t& pos) const;
      int64_t length() const;
      uint64_t id_;
      int64_t use_capacity_;
      int64_t total_capacity_;
      int32_t current_load_;
      int32_t block_count_;
      int32_t last_update_time_;
      int32_t startup_time_;
      Throughput total_tp_;
      int32_t current_time_;
      DataServerLiveStatus status_;
    };

    struct WriteDataInfo
    {
      int deserialize(const char* data, const int64_t data_len, int64_t& pos);
      int serialize(char* data, const int64_t data_len, int64_t& pos) const;
      int64_t length() const;
      uint32_t block_id_;
      uint64_t file_id_;
      int32_t offset_;
      int32_t length_;
      ServerRole is_server_;
      uint64_t file_number_;
    };

    struct CloseFileInfo
    {
      int deserialize(const char* data, const int64_t data_len, int64_t& pos);
      int serialize(char* data, const int64_t data_len, int64_t& pos) const;
      int64_t length() const;
      uint32_t block_id_;
      uint64_t file_id_;
      CloseFileServer mode_;
      uint32_t crc_;
      uint64_t file_number_;
    };

    struct RenameFileInfo
    {
      int deserialize(const char* data, const int64_t data_len, int64_t& pos);
      int serialize(char* data, const int64_t data_len, int64_t& pos) const;
      int64_t length() const;
      uint32_t block_id_;
      uint64_t file_id_;
      uint64_t new_file_id_;
      ServerRole is_server_;
    };

    struct ServerMetaInfo
    {
      int deserialize(const char* data, const int64_t data_len, int64_t& pos);
      int serialize(char* data, const int64_t data_len, int64_t& pos) const;
      int64_t length() const;
      int32_t capacity_;
      int32_t available_;
    };

    struct SegmentHead
    {
      int deserialize(const char* data, const int64_t data_len, int64_t& pos);
      int serialize(char* data, const int64_t data_len, int64_t& pos) const;
      int64_t length() const;
      int32_t count_;           // segment count
      int64_t size_;            // total size that segments contain
      char reserve_[SEGMENT_HEAD_RESERVE_SIZE]; // reserve
      SegmentHead() : count_(0), size_(0)
      {
        memset(reserve_, 0, SEGMENT_HEAD_RESERVE_SIZE);
      }
    };

    struct SegmentInfo
    {
      int deserialize(const char* data, const int64_t data_len, int64_t& pos);
      int serialize(char* data, const int64_t data_len, int64_t& pos) const;
      int64_t length() const;
      uint32_t block_id_;       // block id
      uint64_t file_id_;        // file id
      int64_t offset_;          // offset in current file
      int32_t size_;            // size of segment
      int32_t crc_;             // crc checksum of segment

      SegmentInfo()
      {
        memset(this, 0, sizeof(*this));
      }
      SegmentInfo(const SegmentInfo& seg_info)
      {
        memcpy(this, &seg_info, sizeof(SegmentInfo));
      }
      bool operator < (const SegmentInfo& si) const
      {
        return offset_ < si.offset_;
      }
    };

    struct IpAddr
    {
      uint32_t ip_;
      int32_t port_;
    };

    struct ClientCmdInformation
    {
      int64_t value1_;
      int64_t value2_;
      int32_t value3_;
      int32_t value4_;
      int32_t  cmd_;
      int serialize(char* data, const int64_t data_len, int64_t& pos) const;
      int deserialize(const char* data, const int64_t data_len, int64_t& pos);
      int64_t length() const;
    };

#pragma pack()

    struct CrcCheckFile
    {
      uint32_t block_id_;
      uint64_t file_id_;
      uint32_t crc_;
      CheckDsBlockType flag_;
      VUINT64 fail_servers_;

      CrcCheckFile() :
        block_id_(0), file_id_(0), crc_(0), flag_(CRC_DS_PATIAL_ERROR)
      {
        fail_servers_.clear();
      }

      CrcCheckFile(const uint32_t block_id, const CheckDsBlockType flag) :
        block_id_(block_id), file_id_(0), crc_(0), flag_(flag)
      {
        fail_servers_.clear();
      }
    };

    struct DsTask
    {
      DsTask() {}
      DsTask(const uint64_t server_id, const int32_t cluster_id = 0) :
        server_id_(server_id), cluster_id_(cluster_id) {}
      ~DsTask() {}
      uint64_t server_id_;
      uint64_t old_file_id_;
      uint64_t new_file_id_;
      int32_t cluster_id_;
      uint32_t block_id_;
      int32_t num_row_;
      //data member, used in list block
      int32_t list_block_type_;
      //data member, used in unlink file
      int32_t unlink_type_;
      int32_t option_flag_;
      //true: 0; false: 1.
      int32_t is_master_;
      //data member, used in read file info
      int32_t mode_;
      uint32_t crc_;
      char local_file_[MAX_PATH_LENGTH];
      VUINT64 failed_servers_;
    };

    struct MMapOption
    {
      int32_t max_mmap_size_;
      int32_t first_mmap_size_;
      int32_t per_mmap_size_;

      int serialize(char* data, const int64_t data_len, int64_t& pos) const;
      int deserialize(const char* data, const int64_t data_len, int64_t& pos);
      int64_t length() const;
    };

    struct SuperBlock
    {
      char mount_tag_[MAX_DEV_TAG_LEN]; // magic tag
      int32_t time_;                    // mount time
      char mount_point_[common::MAX_DEV_NAME_LEN]; // name of mount point
      int64_t mount_point_use_space_; // the max space of the mount point
      BaseFsType base_fs_type_; // ext4, ext3...

      int32_t superblock_reserve_offset_; // super block start offset. not used
      int32_t bitmap_start_offset_; // bitmap start offset

      int32_t avg_segment_size_; // avg file size in block file

      float block_type_ratio_; // block type ration
      int32_t main_block_count_; // total count of main block
      int32_t main_block_size_; // per size of main block

      int32_t extend_block_count_; // total count of ext block
      int32_t extend_block_size_; // per size of ext block

      int32_t used_block_count_; // used main block count
      int32_t used_extend_block_count_; // used ext block count

      float hash_slot_ratio_; // hash slot count / file count ratio
      int32_t hash_slot_size_; // number of hash bucket slot
      MMapOption mmap_option_; // mmap option
      int32_t version_; // version

      int serialize(char* data, const int64_t data_len, int64_t& pos) const;
      int deserialize(const char* data, const int64_t data_len, int64_t& pos);
      int64_t length() const;
      void display() const;
    };

    enum MetaActionOp
    {
      NON_ACTION = 0,
      CREATE_DIR = 1,
      CREATE_FILE = 2,
      REMOVE_DIR = 3,
      REMOVE_FILE = 4,
      MOVE_DIR = 5,
      MOVE_FILE = 6
    };

    typedef enum _RemoveBlockResponseFlag
    {
      REMOVE_BLOCK_RESPONSE_FLAG_NO = 0,
      REMOVE_BLOCK_RESPONSE_FLAG_YES = 1
    }RemoveBlockResponseFlag;

    typedef enum _ClearSystemTableFlag
    {
      CLEAR_SYSTEM_TABLE_FLAG_TASK = 1,
      CLEAR_SYSTEM_TABLE_FLAG_WRITE_BLOCK = 1 << 1,
      CLEAR_SYSTEM_TABLE_FLAG_REPORT_SERVER = 1 << 2,
      CLEAR_SYSTEM_TABLE_FLAG_DELETE_QUEUE  = 1 << 3
    }ClearSystemTableFlag;
    typedef enum _FamilyMemberStatus
    {
      FAMILY_MEMBER_STATUS_NORMAL = 0,
      FAMILY_MEMBER_STATUS_ABNORMAL = 1,
      FAMILY_MEMBER_STATUS_OTHER = 2
    }FamilyMemberStatus;

    typedef enum _RemoveOrReinstateBlockType
    {
      REMOVE_OR_REINSTATE_TYPE_DIRECT = 0, //直接删除
      REMOVE_OR_REINSTATE_TYPE_OTHER       //当前BLOCK有可能会被删除，也有可能需要将FAMILY ID置空，重新汇报上来
    }RemoveOrReinstateBlockType;

    struct FamilyInfo
    {
      int64_t family_id_;
      int32_t family_aid_info_;
      std::vector<std::pair<uint32_t, int32_t> > family_member_;
      int deserialize(const char* data, const int64_t data_len, int64_t& pos);
      int serialize(char* data, const int64_t data_len, int64_t& pos) const;
      int64_t length() const;
    };

    struct FamilyMemberInfo
    {
      uint64_t server_;
      uint32_t block_;
      int32_t  version_;
      int32_t  status_;
      int deserialize(const char* data, const int64_t data_len, int64_t& pos);
      int serialize(char* data, const int64_t data_len, int64_t& pos) const;
      int64_t length() const;
      bool operator == (const FamilyMemberInfo& info) const;
    };

    struct FamilyInfoExt
    {
      int64_t family_id_;
      int32_t family_aid_info_;
      std::vector<std::pair<uint32_t, uint64_t> > members_;

      int deserialize(const char* data, const int64_t data_len, int64_t& pos);
      int serialize(char* data, const int64_t data_len, int64_t& pos) const;
      int64_t length() const;

      FamilyInfoExt()
      {
        family_id_ = INVALID_FAMILY_ID;
        family_aid_info_ = 0;
      }
    };

    struct BlockInfoSeg
    {
      common::VUINT64 ds_;
      uint32_t lease_id_;
      int32_t version_;
      FamilyInfoExt family_info_;
      BlockInfoSeg() : lease_id_(INVALID_LEASE_ID), version_(0)
      {
        ds_.clear();
      }
      BlockInfoSeg(const common::VUINT64& ds,
          const uint32_t lease_id = INVALID_LEASE_ID, const int32_t version = 0) :
        ds_(ds), lease_id_(lease_id), version_(version)
      {
      }
      bool has_lease() const
      {
        return (lease_id_ != INVALID_LEASE_ID);
      }
    };

    struct RawIndex
    {
      uint32_t block_id_;
      char* data_;
      uint32_t size_;

      RawIndex(): block_id_(0), data_(NULL), size_(0)
      {
      }

      RawIndex(const uint32_t block_id, char* data, const uint32_t size)
      {
        block_id_ = block_id;
        data_ = data;
        size_ = size;
      }
    };

    enum RawIndexOp
    {
      OP_NOT_INIT,
      WRITE_DATA_INDEX,
      WRITE_PARITY_INDEX,
      READ_DATA_INDEX,
      READ_PARITY_INDEX
    };

    enum RawDataType
    {
      NORMAL_DATA = 1,
      PARITY_DATA
    };

    typedef enum _ReportBlockType
    {
      REPORT_BLOCK_TYPE_ALL = 0,
      REPORT_BLOCK_TYPE_PART,
      REPORT_BLOCK_TYPE_RELIEVE
    }ReportBlockType;

    typedef enum NodeStatus
    {
      NODE_ALIVE = 0,
      NODE_DEAD = 1,
      NODE_UNUSED = -1
    };

    extern const char* dynamic_parameter_str[43];

    // defined type typedef
    typedef std::vector<BlockInfo> BLOCK_INFO_LIST;
    typedef std::vector<FileInfo> FILE_INFO_LIST;
    typedef std::map<uint64_t, FileInfo*> FILE_INFO_MAP;
    typedef FILE_INFO_MAP::iterator FILE_INFO_MAP_ITER;

    typedef std::vector<RawMeta> RawMetaVec;
    typedef std::vector<RawMeta>::iterator RawMetaVecIter;
    typedef std::vector<RawMeta>::const_iterator RawMetaVecConstIter;

    typedef std::vector<RawIndex> RawIndexVec;
    typedef std::vector<RawIndex>::iterator RawIndexVecIter;
    typedef std::vector<RawIndex>::const_iterator RawIndexVecConstIter;

    typedef std::vector<CheckBlockInfo> CheckBlockInfoVec;
    typedef std::vector<CheckBlockInfo>::iterator CheckBlockInfoVecIter;
    typedef std::vector<CheckBlockInfo>::const_iterator CheckBlockInfoVecConstIter;

    typedef __gnu_cxx::hash_map<uint32_t, CheckBlockInfoVec> CheckBlockInfoMap;
    typedef __gnu_cxx::hash_map<uint32_t, CheckBlockInfoVec>::iterator CheckBlockInfoMapIter;
    typedef __gnu_cxx::hash_map<uint32_t, CheckBlockInfoVec>::const_iterator CheckBlockInfoMapConstIter;

    static const int32_t FILEINFO_SIZE = sizeof(FileInfo);
    static const int32_t BLOCKINFO_SIZE = sizeof(BlockInfo);
    static const int32_t RAW_META_SIZE = sizeof(RawMeta);

    enum identify_id
    {
      //TfsFileInfo struct
      TFS_FILE_INFO_CLUSTER_ID_TAG = 101,
      TFS_FILE_INFO_BLOCK_ID_TAG = 102,
      TFS_FILE_INFO_FILE_ID_TAG = 103,
      TFS_FILE_INFO_OFFSET_TAG = 104,
      TFS_FILE_INFO_FILE_SIZE_TAG = 105,

      //ObjectMetaInfo struct
      OBJECT_META_INFO_CREATE_TIME_TAG = 201,
      OBJECT_META_INFO_MODIFY_TIME_TAG = 202,
      OBJECT_META_INFO_MAX_TFS_FILE_SIZE_TAG = 203,
      OBJECT_META_INFO_BIG_FILE_SIZE_TAG = 204,
      OBJECT_META_INFO_OWNER_ID_TAG = 205,

      //CustomizeInfo struct
      CUSTOMIZE_INFO_OTAG_TAG = 301,

      //ObjectInfo struct
      OBJECT_INFO_HAS_META_INFO_TAG = 401,
      OBJECT_INFO_HAS_CUSTOMIZE_INFO_TAG = 402,
      OBJECT_INFO_META_INFO_TAG = 403,
      OBJECT_INFO_V_TFS_FILE_INFO_TAG = 404,
      OBJECT_INFO_CUSTOMIZE_INFO_TAG = 405,

      //BucketMetaInfo struct
      BUCKET_META_INFO_CREATE_TIME_TAG = 501,
      BUCKET_META_INFO_OWNER_ID_TAG = 502,

      //KvMetaTable
      KV_META_TABLE_V_META_TABLE_TAG = 601,

      //UserInfo
      USER_INFO_OWNER_ID_TAG = 801,

      //End TAG
      END_TAG = 999
    };

  }/** end namespace common*/
}/** end namespace tfs **/

#endif //TFS_COMMON_INTERNAL_H_
