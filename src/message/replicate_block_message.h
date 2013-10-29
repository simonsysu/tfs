/*
 * (C) 2007-2010 Alibaba Group Holding Limited.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 *
 * Version: $Id: replicate_block_message.h 477 2011-06-10 08:15:48Z duanfei@taobao.com $
 *
 * Authors:
 *   duolong <duolong@taobao.com>
 *      - initial release
 *
 */
#ifndef TFS_MESSAGE_REPLICATEBLOCKMESSAGE_H_
#define TFS_MESSAGE_REPLICATEBLOCKMESSAGE_H_
#include "base_task_message.h"

namespace tfs
{
  namespace message
  {
    class ReplicateBlockMessage: public BaseTaskMessage
    {
      public:
        ReplicateBlockMessage();
        virtual ~ReplicateBlockMessage();
        virtual int serialize(common::Stream& output) const ;
        virtual int deserialize(common::Stream& input);
        int deserialize(const char* data, const int64_t data_len, int64_t& pos);
        virtual int64_t length() const;
        void dump(void) const;
        inline void set_status(const int32_t status)
        {
          status_ = status;
        }
        inline int32_t get_status() const
        {
          return status_;
        }
        inline common::ReplicateBlockMoveFlag get_move_flag() const
        {
          return static_cast<common::ReplicateBlockMoveFlag>(repl_block_.is_move_);
        }

        inline void set_repl_block(const common::ReplBlock* repl_block)
        {
          if (NULL != repl_block)
          {
            repl_block_ = *repl_block;
          }
        }
        inline const common::ReplBlock* get_repl_block() const
        {
          return &repl_block_;
        }
      protected:
        int32_t status_;
        common::ReplBlock repl_block_;
    };
  }
}
#endif
