# 第三方组件声明

本产品包含或随附以下第三方组件。各组件版权归其各自所有者所有，按各自许可证条款使用与分发。本声明随产品一并提供。

## 1. Lua 5.4.7（Gvtcier 脚本引擎基础）

- 用途：内嵌脚本语言解释器（词法/解析/执行），源码集成并静态编译进 Gvtcier.dll
- 版本：5.4.7（以 Gvts 名称集成，符号前缀由 lua/gvts 体系改名而来）
- 许可证：MIT License
- 版权：Copyright (c) 1994-2024 Lua.org, PUC-Rio
- 位置：src\GvtcierScript\（源码）
- 说明：为适配 Gvtcier 引擎已修改源码（符号改名、关键字中文化、UTF-8 词法支持、全角/半角归一），修改后仍按 MIT 授权
- 许可全文：见附录 A.5

## 2. MinGW-w64 / GCC 编译工具链

- 用途：随 SDK 分发的编译工具链（tools\Mingw），用于构建本产品
- 版本：GCC 11.2.0（x86_64-w64-mingw32，winlibs 发行版）
- 许可证：
  - GCC 编译器及工具链：GNU GPL v3，附带 GCC Runtime Library Exception 3.1（见附录 A.2）
  - MinGW-w64 运行时库与头文件：Zope Public License 2.1（见附录 A.3）
- 说明：根据 GCC Runtime Library Exception，使用本工具链编译本产品所得的目标代码（Gvtcier.dll、main.exe 等）不受 GPL 条款约束；仅当再分发工具链本身时适用 GPL。
- 位置：tools\Mingw\

## 3. Microsoft Windows SDK（Win32 API）

- 用途：操作系统提供的系统库与头文件（user32.lib、gdi32.lib、opengl32.lib、gdiplus.lib、msimg32.lib 等），随 Windows 操作系统提供
- 说明：非本产品分发内容；使用时遵守微软操作系统许可条款。

## 4. Python 3.13（构建脚本解释器）

- 用途：构建脚本解释器（build.py 由 tools\Python313\python.exe 运行），随 SDK 分发
- 版本：3.13.2
- 许可证：PSF License Agreement（Python Software Foundation）
- 版权：Copyright (c) 2001-2024 Python Software Foundation; All Rights Reserved
- 位置：tools\Python313\
- 许可全文：tools\Python313\LICENSE.txt（随附），或见 https://docs.python.org/3/license.html

## 5. miniaudio

- 用途：音频播放与采集库，用于引擎音频层（miniaudio.c 实现已接入 Gvtcier.dll 编译）
- 版本：0.11.25（2026-03-04）
- 许可证：公有领域（Public Domain）或 MIT No Attribution（MIT-0），由使用者二选一
- 版权：Copyright David Reid（mackron@gmail.com）
- 位置：Thirdparty/miniaudio.h
- 许可全文：见附录 A.6

## 6. tinygltf

- 用途：glTF 2.0 模型加载库，用于替换自研 json.h 解析层
- 许可证：MIT License
- 版权：Copyright (c) 2015-2018 Syoyo Fujita, Aurélien Chatelain and many contributors
- 位置：Thirdparty/tiny_gltf.h、Thirdparty/json.hpp
- 随附依赖（同目录分发）：
  - json.hpp（nlohmann/json 3.5.0）：MIT License，Copyright (c) 2013-2018 Niels Lohmann（见附录 A.8）
  - stb_image.h v2.21：公有领域，Copyright Sean Barrett（nothings.org）
  - stb_image_write.h v1.11：公有领域，Copyright Sean Barrett 2010-2015（nothings.org）
- 许可全文：见附录 A.7

## 7. Jolt Physics

- 用途：刚体动力学与碰撞检测（静态世界、动态刚体、运动学平台、射线与形状查询）
- 版本：随仓库集成的源码版本（源自 Godot 引擎 thirdparty 镜像）
- 许可证：MIT License
- 版权：Copyright 2021 Jorrit Rouwe
- 位置：Thirdparty/Jolt/（源码）
- 说明：源码原样集成，未做修改；引擎物理层直接调用其 PhysicsSystem，静态物体以 Static 刚体形式注册到同一世界

## 8. OpenAL 1.1（运行时动态库）

- 用途：音频输出运行时，引擎音频层经 LoadLibraryA 动态加载（OpenAL32.dll / wrap_oal.dll），随 dist 目录分发
- 版本：1.1（2000-2006 SDK 发行版）
- 许可证：GNU LGPL v2.1（Creative OpenAL SDK 发行版）
- 版权：Copyright (C) 2000-2006，Portions (C) Creative Labs Inc. and NVIDIA Corp.
- 位置：dist\OpenAL32.dll、dist\wrap_oal.dll
- 说明：以独立动态库形式分发，未与本产品静态链接；依据 LGPL，用户可自由替换该组件。LGPL 条款仅约束这两个 DLL 本身，不改变本产品其余部分的专有性质。

## 9. stb_truetype（stb 单头文件库）

- 用途：字体解析与字形栅格化，引擎文本渲染由它生成 8 位抗锯齿字形位图
- 版本：随 Thirdparty/stb 集成的源码版本
- 许可证：公有领域（Public Domain）或 MIT License，由使用者二选一
- 版权：Copyright (c) 2017 Sean Barrett
- 位置：Thirdparty/stb/stb_truetype.h
- 说明：源码原样集成，未做修改

## 10. 思源黑体 Source Han Sans（内嵌字体）

- 用途：界面与游戏内文本渲染，随产品内嵌分发
- 版本：Source Han Sans CN（思源黑体简体中文），Regular 与 Bold 各一份
- 许可证：SIL Open Font License 1.1（OFL-1.1）
- 版权：Copyright 2014-2025 Adobe (http://www.adobe.com/), with Reserved Font Name 'Source'
- 位置：assets/Fonts/（打包进 Gvtcier.dll 的 res.rc 资源）
- 说明：允许商用、嵌入与随产品再分发，无需支付费用。本产品已将字体内部族名改为 Gvtcier_Font 以便与系统字体隔离，此举属于 OFL 允许的修改；字体文件本身不单独售卖，修改后的字体仍按 OFL 1.1 授权。OFL 要求随字体一并提供许可证文本，本声明已满足该要求。

## 11. Diligent Engine（图形渲染层）

- 用途：跨平台图形渲染库，引擎 RHI 层的后端实现来源（Vulkan / OpenGL / Direct3D 11 / Direct3D 12）
- 版本：随仓库集成的源码版本（DiligentCore）
- 许可证：Apache License 2.0
- 版权：Copyright 2019-2026 Diligent Graphics LLC；Copyright 2015-2019 Egor Yusov
- 位置：src\Rhidriver\dg\核心\（源码，仅保留引擎真正使用的模块）
- 说明：源码集成并静态编译进 Gvtcier.dll。仅取用了 Primitives / Common / Platforms / Graphics 下的渲染核心与四个后端，去掉了 Metal、WebGPU、Archiver、.NET 绑定、CMake 构建脚本与文档目录。为接入引擎 RHI 接口层，将在其外围新增自定义封装代码，修改后的文件按 Apache 2.0 第 4(b) 条标注改动。
- 许可全文：见附录 A.10

---

## 附录 A：许可证全文

### A.2 GCC Runtime Library Exception 3.1（摘要）

GCC 编译器本身按 GNU GPL v3 授权；GCC Runtime Library Exception 3.1 允许使用 GCC 编译的任何程序（含链接 GCC 运行时库的程序）按程序自身的许可证分发，无需受 GPL 约束。例外适用于使用 GCC 编译器生成的目标代码，以及 GCC 运行时库（libgcc、libstdc++ 等）。完整例外文本见 GCC 文档：https://gcc.gnu.org/onlinedocs/gcc-11.2.0/gcc/ 。

### A.3 Zope Public License 2.1（MinGW-w64 运行时，摘要）

MinGW-w64 运行时库（mingwrt）按 Zope Public License 2.1 授权，允许以源代码或二进制形式使用、复制、修改与再分发，条件为保留版权声明与许可声明。完整文本见 MinGW-w64 项目：https://mingw-w64.org/ 。

### A.4 PSF License Agreement（Python 3.13，摘要）

Python 3.13 按 PSF License Agreement 授权（版权：Copyright (c) 2001-2024 Python Software Foundation; All Rights Reserved），允许以源代码或二进制形式使用、复制、修改与再分发，条件为保留 PSF 版权声明与本许可声明。完整文本随附于 tools\Python313\LICENSE.txt，亦可于 https://docs.python.org/3/license.html 查阅。

### A.5 MIT License（Lua 5.4.7，Gvtcier 脚本引擎基础）

Copyright (c) 1994-2024 Lua.org, PUC-Rio

Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated documentation files (the "Software"), to deal in the Software without restriction, including without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the Software, and to permit persons to whom the Software is furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

### A.6 miniaudio（公有领域 / MIT No Attribution）

miniaudio 由作者 David Reid 按"公有领域（Public Domain）或 MIT No Attribution（MIT-0）二选一"发布。公有领域声明免除一切版权限制；MIT-0 全文如下：

MIT No Attribution

Permission is hereby granted, free of charge, to any person obtaining a copy of
this software and associated documentation files (the "Software"), to deal in
the Software without restriction, including without limitation the rights to
use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies
of the Software, and to permit persons to whom the Software is furnished to do
so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.

### A.7 MIT License（tinygltf）

The MIT License (MIT)

Copyright (c) 2015 - 2018 Syoyo Fujita, Aurélien Chatelain and many contributors

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.

### A.8 MIT License（nlohmann/json）

MIT License

Copyright (c) 2013-2018 Niels Lohmann

Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated documentation files (the "Software"), to deal in the Software without restriction, including without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the Software, and to permit persons to whom the Software is furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

### A.9 GNU LGPL v2.1（OpenAL 1.1 运行时，摘要）

OpenAL 1.1 运行时（OpenAL32.dll / wrap_oal.dll）按 GNU Lesser General Public License v2.1 授权。LGPL 允许将本库以动态链接方式随专有软件一并分发，条件为：保留版权与许可声明、以独立文件形式分发该库（本产品即如此）、并向用户提供替换该库的可行途径。完整文本见 https://www.gnu.org/licenses/old-licenses/lgpl-2.1.html 。

### A.10 Apache License 2.0（Diligent Engine）

Diligent Engine 按 Apache License Version 2.0（2004 年 1 月）授权，版权归 Diligent Graphics LLC 与 Egor Yusov 所有。该许可证允许以源代码或目标代码形式使用、复制、修改与再分发，条件为：随附许可证副本；修改过的文件标注改动说明；保留原版权、专利、商标与归属声明；若原作品含 NOTICE 文件则一并保留其归属信息。

全文见 Apache 基金会官方发布：https://www.apache.org/licenses/LICENSE-2.0 。随源码一并保留的许可证原文位于 src\Rhidriver\dg\核心\LICENSE.txt。
