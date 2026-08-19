#pragma once

#include <PolygonClippingSpecJSI.h>

#include <memory>
#include <vector>

namespace facebook::react {

class PolygonClippingImpl : public NativePolygonClippingCxxSpec<PolygonClippingImpl> {
 public:
  PolygonClippingImpl(std::shared_ptr<CallInvoker> jsInvoker);

  std::vector<double> clip(jsi::Runtime& rt, double op, std::vector<double> geometry);
};

} // namespace facebook::react
