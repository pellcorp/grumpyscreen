#ifndef __NOTIFY_CONSUMER_H__
#define __NOTIFY_CONSUMER_H__

#include "hv/json.hpp"
#include <mutex>

using json = nlohmann::json;

class NotifyConsumer {
 public:
  NotifyConsumer(std::mutex &lv_lock);
  ~NotifyConsumer();
  virtual void consume(nlohmann::json &data) = 0;
  // Status updates for the rest are held while the display sleeps and
  // delivered merged on wake; anything keeping a history has to see each one
  virtual bool consume_while_asleep() const { return false; }
  // virtual void consume(std::string &str) = 0;
 protected:
  std::mutex &lv_lock;
};

#endif // __NOTIFY_CONSUMER_H__
