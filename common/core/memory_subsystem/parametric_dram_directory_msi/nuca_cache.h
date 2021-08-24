#ifndef __NUCA_CACHE_H
#define __NUCA_CACHE_H

#include "fixed_types.h"
#include "subsecond_time.h"
#include "hit_where.h"
#include "cache_cntlr.h"

#include "boost/tuple/tuple.hpp"

class MemoryManagerBase;
class ShmemPerfModel;
class AddressHomeLookup;
class QueueModel;
class ShmemPerf;

class NucaCache
{
   public:
      enum pcm_last_op_t
      {
         INVALID = 0,
         READ,
         WRITE
      };

   private:
      core_id_t m_core_id;
      MemoryManagerBase *m_memory_manager;
      ShmemPerfModel *m_shmem_perf_model;
      AddressHomeLookup *m_home_lookup;
      UInt32 m_cache_block_size;
      ComponentLatency m_data_access_time;
      ComponentLatency m_tags_access_time;
      ComponentLatency m_pcm_write_time;
      ComponentBandwidth m_data_array_bandwidth;

      Cache* m_cache;
      QueueModel *m_queue_model;

      UInt64 m_reads, m_writes, m_read_misses, m_write_misses;

      ShmemPerf m_dummy_shmem_perf;

      SubsecondTime accessDataArray(Cache::access_t access, SubsecondTime t_start, ShmemPerf *perf);

      std::unordered_map<IntPtr, UInt64> m_addr_reads;
      std::unordered_map<IntPtr, UInt64> m_addr_writes;
      // std::map<UInt64, UInt64> m_read_service_count;
      // std::map<UInt64, UInt64> m_write_service_count;
      // UInt64 m_max_buckets = 200;

      std::map<UInt64, UInt64> m_rw_service_count;
      UInt64 m_max_bin_buckets = 11;
      std::map<String, UInt64> m_bin_bucket_names = {{"0", 0}, {"1/16", 1}, {"1/8", 2}, {"1/4", 3}, {"1/2", 4}, {"1", 5}, {"2", 6}, {"4", 7}, {"8", 8}, {"16", 9}, {"inf", 10}};
      std::map<double, UInt64> m_float_bucket_names = {{0.0, 0}, {0.0625, 1}, {0.125, 2}, {0.25, 3}, {0.5, 4}, {1, 5}, {2, 6}, {4, 7}, {8, 8}, {16, 9}};
      /* m_rw_service_count shoule be: 1/16, 1/8, 1/4, 1/2, 1, 2, 4, 8, 16, */

      // extra PCM stats
      UInt64 rar, war, raw, waw, evict_aw, evict_ar;
      std::map<IntPtr, pcm_last_op_t> m_last_op;

   public:

      // enum pcm_extra_stat_t
      //    {
      //       READ_AFTER_READ = 0,
      //       READ_AFTER_WRITE,
      //       WRITE_AFTER_WRITE,
      //       WRITE_AFTER_READ,
      //       EVICT_AFTER_READ,
      //       EVICT_AFTER_WRITE
      //    };

      NucaCache(MemoryManagerBase* memory_manager, ShmemPerfModel* shmem_perf_model, AddressHomeLookup* home_lookup, UInt32 cache_block_size, ParametricDramDirectoryMSI::CacheParameters& parameters);
      ~NucaCache();

      boost::tuple<SubsecondTime, HitWhere::where_t> read(IntPtr address, Byte* data_buf, SubsecondTime now, ShmemPerf *perf, bool count);
      boost::tuple<SubsecondTime, HitWhere::where_t> write(IntPtr address, Byte* data_buf, bool& eviction, IntPtr& evict_address, Byte* evict_buf, SubsecondTime now, bool count);

      // Drake: inclusion
      ShmemPerf* getShmemPerfModel(){ return &m_dummy_shmem_perf; };

      double getNearestPower(double n);

      void dumpRemainingRW();
      static SInt64 __dumpRemainingRW(UInt64 arg0, UInt64 arg1) { ((NucaCache*)arg0)->dumpRemainingRW(); return 0; }
      void updateLastOp(IntPtr address, pcm_last_op_t new_state);
};

#endif // __NUCA_CACHE_H
