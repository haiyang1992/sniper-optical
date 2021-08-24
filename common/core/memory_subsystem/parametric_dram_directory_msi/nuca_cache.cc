#include "nuca_cache.h"
#include "memory_manager_base.h"
#include "pr_l1_cache_block_info.h"
#include "config.hpp"
#include "stats.h"
#include "queue_model.h"
#include "shmem_perf.h"

#include "hooks_manager.h"

#include <cmath>

NucaCache::NucaCache(MemoryManagerBase* memory_manager, ShmemPerfModel* shmem_perf_model, AddressHomeLookup* home_lookup, UInt32 cache_block_size, ParametricDramDirectoryMSI::CacheParameters& parameters)
   : m_core_id(memory_manager->getCore()->getId())
   , m_memory_manager(memory_manager)
   , m_shmem_perf_model(shmem_perf_model)
   , m_home_lookup(home_lookup)
   , m_cache_block_size(cache_block_size)
   , m_data_access_time(parameters.data_access_time)
   , m_tags_access_time(parameters.tags_access_time)
   , m_pcm_write_time(parameters.pcm_write_time)
   , m_data_array_bandwidth(8 * Sim()->getCfg()->getFloat("perf_model/nuca/bandwidth"))
   , m_queue_model(NULL)
   , m_reads(0)
   , m_writes(0)
   , m_read_misses(0)
   , m_write_misses(0)
{
   m_cache = new Cache("nuca-cache",
      "perf_model/nuca/cache",
      m_core_id,
      parameters.num_sets,
      parameters.associativity,
      m_cache_block_size,
      parameters.replacement_policy,
      CacheBase::PR_L1_CACHE,
      CacheBase::parseAddressHash(parameters.hash_function),
      NULL, /* FaultinjectionManager */
      home_lookup
   );

   if (Sim()->getCfg()->getBool("perf_model/nuca/queue_model/enabled"))
   {
      String queue_model_type = Sim()->getCfg()->getString("perf_model/nuca/queue_model/type");
      m_queue_model = QueueModel::create("nuca-cache-queue", m_core_id, queue_model_type, m_data_array_bandwidth.getRoundedLatency(8 * m_cache_block_size)); // bytes to bits
   }

   // Drake: initialize maps
   if (m_core_id == 0)
   {
      // for (UInt64 i = 0; i < m_max_buckets; i++)
      // {
      //    m_read_service_count[i] = 0;
      //    m_write_service_count[i] = 0;
      // }
      for (UInt64 i = 0; i < m_max_bin_buckets; i++)
      {
         m_rw_service_count[i] = 0;
      }
   }

   registerStatsMetric("nuca-cache", m_core_id, "reads", &m_reads);
   registerStatsMetric("nuca-cache", m_core_id, "writes", &m_writes);
   registerStatsMetric("nuca-cache", m_core_id, "read-misses", &m_read_misses);
   registerStatsMetric("nuca-cache", m_core_id, "write-misses", &m_write_misses);

   if (m_core_id == 0)
   {
      // for (UInt64 i = 0; i < m_max_buckets; i++)
      // {
      //    String read_name = String("read_service_count") + "[" + itostr(i) + "]";
      //    registerStatsMetric("nuca-cache", m_core_id, read_name, &(m_read_service_count[i]));
      //    String write_name = String("write_service_count") + "[" + itostr(i) + "]";
      //    registerStatsMetric("nuca-cache", m_core_id, write_name, &(m_write_service_count[i]));
      // }

      std::map<UInt64, String> bin_bucket_names = {{0, "0"}, {1, "1/16"}, {2, "1/8"}, {3, "1/4"}, {4, "1/2"}, {5, "1"}, {6, "2"}, {7, "4"}, {8, "8"}, {9, "16"}, {10, "inf"}};
      for (UInt64 i = 0; i < m_max_bin_buckets; i++)
      {
         String name = String("rw_service_count[") + bin_bucket_names[i] + "]";
         registerStatsMetric("nuca-cache", m_core_id, name , &(m_rw_service_count[i]));
      }

      rar = 0;
      raw = 0;
      war = 0;
      waw = 0;
      evict_ar = 0;
      evict_aw = 0;
      registerStatsMetric("nuca-cache", m_core_id, "read-after-read", &rar);
      registerStatsMetric("nuca-cache", m_core_id, "read-after-write", &raw);
      registerStatsMetric("nuca-cache", m_core_id, "write-after-read", &war);
      registerStatsMetric("nuca-cache", m_core_id, "write-after-write", &waw);
      registerStatsMetric("nuca-cache", m_core_id, "evict-after-read", &evict_ar);
      registerStatsMetric("nuca-cache", m_core_id, "evict-after-write", &evict_aw);

      Sim()->getHooksManager()->registerHook(HookType::HOOK_ROI_END, __dumpRemainingRW, (UInt64)this, HooksManager::ORDER_NOTIFY_PRE);
   }

}

NucaCache::~NucaCache()
{
   delete m_cache;
   if (m_queue_model)
      delete m_queue_model;
}

boost::tuple<SubsecondTime, HitWhere::where_t>
NucaCache::read(IntPtr address, Byte* data_buf, SubsecondTime now, ShmemPerf *perf, bool count)
{
   HitWhere::where_t hit_where = HitWhere::MISS;
   perf->updateTime(now);

   PrL1CacheBlockInfo* block_info = (PrL1CacheBlockInfo*)m_cache->peekSingleLine(address);
   SubsecondTime latency = m_tags_access_time.getLatency();
   perf->updateTime(now + latency, ShmemPerf::NUCA_TAGS);

   if (block_info)
   {
      m_cache->accessSingleLine(address, Cache::LOAD, data_buf, m_cache_block_size, now + latency, true);

      latency += accessDataArray(Cache::LOAD, now + latency, perf);
      hit_where = HitWhere::NUCA_CACHE;
      // Drake:
      // read hit, increment counter
      if (!m_addr_reads.count(address))
      {
         m_addr_reads[address] = 1;
      }
      else
      {
         m_addr_reads[address]++;
      }
      // if (!m_addr_writes.count(address))
      // {
      //    m_addr_writes[address] = 0;
      // }
   }
   else
   {
      // if (count) ++m_read_misses;
      ++m_read_misses;
   }
   if (count) ++m_reads;

   updateLastOp(address, NucaCache::READ);
   return boost::tuple<SubsecondTime, HitWhere::where_t>(latency, hit_where);
}

boost::tuple<SubsecondTime, HitWhere::where_t>
NucaCache::write(IntPtr address, Byte* data_buf, bool& eviction, IntPtr& evict_address, Byte* evict_buf, SubsecondTime now, bool count)
{
   HitWhere::where_t hit_where = HitWhere::MISS;

   PrL1CacheBlockInfo* block_info = (PrL1CacheBlockInfo*)m_cache->peekSingleLine(address);
   SubsecondTime latency = m_tags_access_time.getLatency();

   if (block_info)
   {
      block_info->setCState(CacheState::MODIFIED);
      m_cache->accessSingleLine(address, Cache::STORE, data_buf, m_cache_block_size, now + latency, true);

      latency += accessDataArray(Cache::STORE, now + latency, &m_dummy_shmem_perf);
      hit_where = HitWhere::NUCA_CACHE;
      // Drake:
      // write hit, increment counter
      if (!m_addr_writes.count(address))
      {
         m_addr_writes[address] = 1;
      }
      else
      {
         m_addr_writes[address]++;
      }
   }
   else
   {
      PrL1CacheBlockInfo evict_block_info;

      m_cache->insertSingleLine(address, data_buf,
         &eviction, &evict_address, &evict_block_info, evict_buf,
         now + latency);

      if (eviction)
      {
         updateLastOp(evict_address, NucaCache::INVALID);
         if (evict_block_info.getCState() != CacheState::MODIFIED)
         {
            // Unless data is dirty, don't have caller write it back
            eviction = false;
         }
      }

      // if (count) ++m_write_misses;
      ++m_write_misses;

      // Drake:
      // write miss, still a write to new block
      if (!m_addr_writes.count(address))
      {
         m_addr_writes[address] = 1;
      }
      else
      {
         m_addr_writes[address]++;
      }

      if (evict_address && !evict_block_info.hasOption(CacheBlockInfo::WARMUP))
      {
         if (m_addr_writes.count(evict_address))
         {
            double reads;
            if (m_addr_reads.count(evict_address))
            {
               reads = (double)(m_addr_reads[address] + 3);
            }
            else
            {
               reads = 3.0;
            }
            double wr_ratio = (double)(2 * m_addr_writes[evict_address]) / reads;
            m_rw_service_count[m_float_bucket_names[getNearestPower(wr_ratio)]]++;
         }
         // else if (m_addr_reads.count(evict_address))
         // {
         //    // zero writes, 2w/(r+3) = 0
         //    m_rw_service_count[m_bin_bucket_names["0"]]++;
         // }
         m_addr_reads.erase(evict_address);
         m_addr_writes.erase(evict_address);
      }
   }
   if (count) ++m_writes;

   updateLastOp(address, NucaCache::WRITE);
   return boost::tuple<SubsecondTime, HitWhere::where_t>(latency, hit_where);
}

void NucaCache::dumpRemainingRW()
{
   printf("[SNIPER] Dumping remaining valid entries in NUCA at the end of ROI\n");
   for(UInt32 set_index = 0; set_index < m_cache->getNumSets(); ++set_index)
   {
      for(UInt32 way = 0; way < m_cache->getAssociativity(); ++way)
      {
         CacheBlockInfo *block_info = m_cache->peekBlock(set_index, way);
         if (block_info->isValid() && !block_info->hasOption(CacheBlockInfo::WARMUP))
         {
            IntPtr address = m_cache->tagToAddress(block_info->getTag());
            if (m_addr_writes.count(address))
            {
               double reads;
               if (m_addr_reads.count(address))
               {
                  reads = (double)(m_addr_reads[address] + 3);
               }
               else
               {
                  reads = 3.0;
               }
               double wr_ratio = (double)(2 * m_addr_writes[address]) / reads;
               m_rw_service_count[m_float_bucket_names[getNearestPower(wr_ratio)]]++;
            }
            // else if (m_addr_reads.count(address))
            // {
            //    // zero writes, 2w/(r+3) = 0
            //    m_rw_service_count[m_bin_bucket_names["0"]]++;
            // }
            m_addr_reads.erase(address);
            m_addr_writes.erase(address);
         }
      }
   }
}

double NucaCache::getNearestPower(double n)
{
   // if (n == 0) return 0.0;
   // double log = log2(n);
   // // SInt64 log_floor = floor(log);
   // // SInt64 log_ceil = ceil(log);
   // SInt64 log_round = round(log);
   // if (log_round < -4.0) // n <= 1/16 and is closer to 0
   // {
   //    log_round = -4;
   // }
   // else if (log_round > 4.0)
   // {
   //    log_round = 4;
   // }
   // return pow(2, log_round);
   double bin = 0.0;
   int i;
   for (i = -4;i<4;i++)
   {
      if (n < pow(2, i-2) + pow(2, i-1)) return bin;
      bin = pow(2, i);
   }
   return pow(2, i);
}

SubsecondTime
NucaCache::accessDataArray(Cache::access_t access, SubsecondTime t_start, ShmemPerf *perf)
{
   perf->updateTime(t_start);

   // Compute Queue Delay
   SubsecondTime queue_delay;
   if (m_queue_model)
   {
      SubsecondTime processing_time = m_data_array_bandwidth.getRoundedLatency(8 * m_cache_block_size); // bytes to bits

      queue_delay = processing_time + m_queue_model->computeQueueDelay(t_start, processing_time, m_core_id);

      perf->updateTime(t_start + processing_time, ShmemPerf::NUCA_BUS);
      perf->updateTime(t_start + queue_delay, ShmemPerf::NUCA_QUEUE);
   }
   else
   {
      queue_delay = SubsecondTime::Zero();
   }

   // if (access == Cache::STORE){
   //    perf->updateTime(t_start + queue_delay + m_pcm_write_time.getLatency(), ShmemPerf::NUCA_DATA);

   //    return queue_delay + m_pcm_write_time.getLatency();
   // }

   perf->updateTime(t_start + queue_delay + m_data_access_time.getLatency(), ShmemPerf::NUCA_DATA);

   return queue_delay + m_data_access_time.getLatency();
}

void
NucaCache::updateLastOp(IntPtr address, pcm_last_op_t new_state)
{
   pcm_last_op_t old_state;
   if (m_last_op.count(address))
   {
      old_state = m_last_op[address];
   }
   else
   {
      old_state = NucaCache::INVALID;
   }

   switch (new_state)
   {
      case NucaCache::READ :
         if (old_state == NucaCache::READ) rar++;
         if (old_state == NucaCache::WRITE) raw++;
         m_last_op[address] = NucaCache::READ;
         break;
      case NucaCache::WRITE :
         if (old_state == NucaCache::READ) war++;
         if (old_state == NucaCache::WRITE) waw++;
         m_last_op[address] = NucaCache::WRITE;
         break;
      case NucaCache::INVALID :
         if (old_state == NucaCache::READ) evict_ar++;
         if (old_state == NucaCache::WRITE) evict_aw++;
         m_last_op[address] = NucaCache::INVALID;
         break;
      default:
         return;
   };
}