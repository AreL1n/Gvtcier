#pragma once
#include "bicu/core/export.h"

namespace rlog {

void once(const char* key, const char* fmt, ...);
void reset(const char* key);

GVTCIER_API void Gvtcier_Log_InitFile(const char* pathHint);
GVTCIER_API void Gvtcier_Log_Close();
GVTCIER_API void Gvtcier_Log_Write(const char* fmt, ...);
GVTCIER_API void Gvtcier_Log_Echo(const char* fmt, ...);

//把 printf 的输出接管到日志文件里。GUI 进程没有标准输出，不接管就全丢了。
//宿主程序本身带控制台（开发机直接在终端里跑）时不动，免得控制台看不见东西。
GVTCIER_API void Gvtcier_Log_ForwardStdout();
GVTCIER_API void Gvtcier_Log_StopForward();

//把标准输出里还没搬进日志的内容搬过去。日志入口内部会自己调，外部一般不直接碰。
GVTCIER_API void Gvtcier_Log_Pump();
GVTCIER_API void Gvtcier_Log_PumpErr();

// 一条组件自检结果。成功打「[自检] 名字 成功」，失败打「[自检] 名字 失败: 原因」。
GVTCIER_API void Gvtcier_Log_Check(const char* name, bool ok, const char* detail = nullptr);

// 一条资源加载结果，格式与自检一致，前缀换成「[资源]」。
GVTCIER_API void Gvtcier_Log_Asset(const char* name, bool ok, const char* detail = nullptr);

// 设备/子系统初始化横幅：先打一行「[名字] 初始化成功」，再把每项细节缩进打一行。
// 细节行用「|____」开头，一眼能看出从属关系。
GVTCIER_API void Gvtcier_Log_Banner(const char* name, bool ok, const char* version);
GVTCIER_API void Gvtcier_Log_BannerLine(const char* key, const char* value);

// 阶段标题，用于把日志切成可读的段落。
GVTCIER_API void Gvtcier_Log_Stage(int index, const char* title);

}
