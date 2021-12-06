#include "libgo_switcher.h"

namespace libgo
{

// 初始化函数 (用户需要打开namespace自行实现这个初始化函数)
// 在这个函数体内执行 RoutineSyncPolicy::registerSwitchers函数
void routine_sync_init_callback()
{
    // libgo默认用0级, 用户自定义更多协程支持的时候可以使用1级或更高等级
    RoutineSyncPolicy::registerSwitchers<::co::LibgoSwitcher>(0);
}

} // namespace libgo
