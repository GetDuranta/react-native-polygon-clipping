#import <Foundation/Foundation.h>
#import "PolygonClippingImpl.h"
#import <ReactCommon/CxxTurboModuleUtils.h>

@interface PolygonClippingOnLoad : NSObject
@end

@implementation PolygonClippingOnLoad

using namespace facebook::react;

+ (void)load
{
  registerCxxModuleToGlobalModuleMap(
      std::string(PolygonClippingImpl::kModuleName),
      [](std::shared_ptr<CallInvoker> jsInvoker) {
        return std::make_shared<PolygonClippingImpl>(jsInvoker);
      });
}

@end
