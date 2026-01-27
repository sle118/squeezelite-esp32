#include "MDNSService.h"
#include <arpa/inet.h>
#include <vector>
#include "mdns.h"

using namespace bell;

class implMDNSService : public MDNSService {
 private:
  const std::string type;
  const std::string proto;
  void unregisterService() { mdns_service_remove(type.c_str(), proto.c_str()); }

 public:
  implMDNSService(std::string type, std::string proto)
      : type(type), proto(proto){};
};

/**
 * ESP32 implementation of MDNSService
 * @see https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-reference/protocols/mdns.html
 **/

std::unique_ptr<MDNSService> MDNSService::registerService(
    const std::string& serviceName, const std::string& serviceType,
    const std::string& serviceProto, const std::string& serviceHost,
    int servicePort, const std::map<std::string, std::string> txtData) {
  std::vector<mdns_txt_item_t> txtItems;
  txtItems.reserve(txtData.size());
  for (auto& data : txtData) {
    mdns_txt_item_t item;
    item.key = data.first.c_str();
    item.value = data.second.c_str();
    txtItems.push_back(item);
  }

  mdns_service_add(serviceName.c_str(),  /* instance_name */
                   serviceType.c_str(),  /* service_type */
                   serviceProto.c_str(), /* proto */
                   servicePort,          /* port */
                   txtItems.data(),      /* txt */
                   txtItems.size()       /* num_items */
  );

  return std::make_unique<implMDNSService>(serviceType, serviceProto);
}
