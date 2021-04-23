#ifndef __CACHE_PERF_MODEL_PCM_H__
#define __CACHE_PERF_MODEL_PCM_H__

#include "cache_perf_model.h"

class CachePerfModelPCM : public CachePerfModel
{
private:
   bool m_enabled;
   ComponentLatency m_pcm_write_time;

public:
   CachePerfModelPCM(const ComponentLatency &cache_data_access_time,
                     const ComponentLatency &cache_tags_access_time, const ComponentLatency &pcm_write_time) : CachePerfModel(cache_data_access_time, cache_tags_access_time),
                     m_enabled(false),
                     m_pcm_write_time(pcm_write_time)
   {}
   ~CachePerfModelPCM() {}

   void enable() { m_enabled = true; }
   void disable() { m_enabled = false; }
   bool isEnabled() { return m_enabled; }

   SubsecondTime getLatency(CacheAccess_t access)
   {
      if (!m_enabled)
         return SubsecondTime::Zero();

      switch (access)
      {
      case ACCESS_CACHE_TAGS:
         return m_cache_tags_access_time.getLatency();

      case ACCESS_CACHE_DATA:
      case ACCESS_CACHE_DATA_AND_TAGS:
         return m_cache_data_access_time.getLatency();

      case PCM_WRITE_CACHE_DATA_AND_TAGS:
         return m_pcm_write_time.getLatency();

      default:
         return SubsecondTime::Zero();
      }
   }
};

#endif /* __CACHE_PERF_MODEL_SEQUENTIAL_H__ */