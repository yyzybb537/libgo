#include "libgo_switcher.h"

namespace libgo
{

// 初始化函数 (用户需要打开namespace自行实现这个初始化函数)
// 在这个函数体内执行 RoutineSyncPolicy::registerSwitchers函数
void routine_sync_init_callback()
{
    RoutineSyncPolicy::registerSwitchers<::co::LibgoSwitcher>();
}

} // namespace libgo
