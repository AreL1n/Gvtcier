#pragma once
// OpenGL 1.x 固定管线函数的最小声明集合。
// 引擎主渲染走现代核心 profile，但少数老式绘制（如调试几何）需要 glBegin/glEnd 这一套，
// 核心 profile 不提供它们，所以这里独立声明并按需从 opengl32.dll 动态取地址。
#ifdef _WIN32
#include <windows.h>
#include <GL/gl.h>
#ifndef GLAPIENTRY
#define GLAPIENTRY APIENTRY
#endif
#else
#ifndef GLAPIENTRY
#define GLAPIENTRY
#endif
#endif
#include <cstddef>

namespace gl1 {

// 与 GL 同名类型加 1 后缀，避免与头文件里的 GL 类型、与 gl4.h 的类型重复定义冲突。
typedef unsigned int  GLenum1;      // GL 枚举值
typedef unsigned int  GLbitfield1;  // 位掩码（清屏位、屏障位）
typedef unsigned int  GLuint1;      // 对象名（纹理、缓冲等无符号整数句柄）
typedef int           GLint1;       // 通用有符号整型
typedef int           GLsizei1;     // 尺寸/数量
typedef unsigned char GLboolean1;   // 布尔（GL_TRUE/GL_FALSE）
typedef unsigned char GLubyte1;     // 无符号字节
typedef float         GLfloat1;     // 单精度浮点
typedef double        GLdouble1;    // 双精度浮点（glOrtho 等矩阵函数用）

// 清屏位
constexpr GLenum1 GL1_COLOR_BUFFER_BIT  = 0x00004000;  // GL_COLOR_BUFFER_BIT 清颜色
constexpr GLenum1 GL1_DEPTH_BUFFER_BIT  = 0x00000100;  // GL_DEPTH_BUFFER_BIT 清深度
// 能力开关
constexpr GLenum1 GL1_DEPTH_TEST        = 0x0B71;  // GL_DEPTH_TEST 深度测试
constexpr GLenum1 GL1_CULL_FACE         = 0x0B44;  // GL_CULL_FACE 面剔除
constexpr GLenum1 GL1_BLEND             = 0x0BE2;  // GL_BLEND 混合
constexpr GLenum1 GL1_TEXTURE_2D        = 0x0DE1;  // GL_TEXTURE_2D 二维纹理（固定管线用 glEnable 开启）
constexpr GLenum1 GL1_LIGHTING          = 0x0B50;  // GL_LIGHTING 固定管线光照总开关
constexpr GLenum1 GL1_LIGHT0            = 0x4000;  // GL_LIGHT0 第 0 号固定管线光源
constexpr GLenum1 GL1_COLOR_MATERIAL    = 0x0B57;  // GL_COLOR_MATERIAL 用 glColor 直接驱动材质色
constexpr GLenum1 GL1_NORMALIZE         = 0x0BA1;  // GL_NORMALIZE 自动归一化法线

// 固定管线图元类型
constexpr GLenum1 GL1_POINTS            = 0x0000;  // GL_POINTS 点
constexpr GLenum1 GL1_LINES             = 0x0001;  // GL_LINES 线段
constexpr GLenum1 GL1_LINE_LOOP         = 0x0002;  // GL_LINE_LOOP 闭合折线
constexpr GLenum1 GL1_LINE_STRIP        = 0x0003;  // GL_LINE_STRIP 折线
constexpr GLenum1 GL1_TRIANGLES         = 0x0004;  // GL_TRIANGLES 三角形
constexpr GLenum1 GL1_TRIANGLE_STRIP    = 0x0005;  // GL_TRIANGLE_STRIP 三角带
constexpr GLenum1 GL1_TRIANGLE_FAN      = 0x0006;  // GL_TRIANGLE_FAN 三角扇
constexpr GLenum1 GL1_QUADS             = 0x0007;  // GL_QUADS 四边形（核心 profile 已删除）
constexpr GLenum1 GL1_QUAD_STRIP        = 0x0008;  // GL_QUAD_STRIP 四边形带
constexpr GLenum1 GL1_POLYGON           = 0x0009;  // GL_POLYGON 任意凸多边形

// 矩阵栈相关
constexpr GLenum1 GL1_MATRIX_MODE       = 0x0BA0;  // GL_MATRIX_MODE 查询当前矩阵栈
constexpr GLenum1 GL1_PROJECTION        = 0x1701;  // GL_PROJECTION 投影矩阵栈
constexpr GLenum1 GL1_MODELVIEW         = 0x1700;  // GL_MODELVIEW 模型视图矩阵栈
constexpr GLenum1 GL1_TEXTURE           = 0x1702;  // GL_TEXTURE 纹理矩阵栈
constexpr GLenum1 GL1_VIEWPORT          = 0x0BA2;  // GL_VIEWPORT 查询视口
constexpr GLenum1 GL1_MODELVIEW_MATRIX  = 0x0BA6;  // GL_MODELVIEW_MATRIX 查询模型视图矩阵
constexpr GLenum1 GL1_PROJECTION_MATRIX = 0x0BA7;  // GL_PROJECTION_MATRIX 查询投影矩阵

// 着色模型
constexpr GLenum1 GL1_SMOOTH            = 0x1D01;  // GL_SMOOTH 逐顶点插值着色
constexpr GLenum1 GL1_FLAT              = 0x1D00;  // GL_FLAT 平面着色

// 材质/光源参数
constexpr GLenum1 GL1_AMBIENT           = 0x1200;  // GL_AMBIENT 环境光分量
constexpr GLenum1 GL1_DIFFUSE           = 0x1201;  // GL_DIFFUSE 漫反射分量
constexpr GLenum1 GL1_SPECULAR          = 0x1202;  // GL_SPECULAR 高光分量
constexpr GLenum1 GL1_POSITION          = 0x1203;  // GL_POSITION 光源位置/方向
constexpr GLenum1 GL1_AMBIENT_AND_DIFFUSE = 0x1602;  // GL_AMBIENT_AND_DIFFUSE 同时设置环境与漫反射

// 比较函数、面与混合因子
constexpr GLenum1 GL1_LESS              = 0x0201;  // GL_LESS
constexpr GLenum1 GL1_LEQUAL            = 0x0203;  // GL_LEQUAL
constexpr GLenum1 GL1_ALWAYS            = 0x0207;  // GL_ALWAYS
constexpr GLenum1 GL1_BACK              = 0x0405;  // GL_BACK 背面
constexpr GLenum1 GL1_FRONT             = 0x0404;  // GL_FRONT 正面
constexpr GLenum1 GL1_FRONT_AND_BACK    = 0x0408;  // GL_FRONT_AND_BACK 正反两面
constexpr GLenum1 GL1_SRC_ALPHA         = 0x0302;  // GL_SRC_ALPHA 源 alpha 混合因子
constexpr GLenum1 GL1_ONE_MINUS_SRC_ALPHA = 0x0303;// GL_ONE_MINUS_SRC_ALPHA 1-源 alpha
constexpr GLenum1 GL1_TRUE              = 1;       // GL_TRUE
constexpr GLenum1 GL1_FALSE             = 0;       // GL_FALSE

// 固定管线函数指针类型。这些接口在核心 profile 下不存在，必须运行时取地址；
// 用 PFN_ 前缀与 GL 原名区分。
typedef void (GLAPIENTRY *PFN_glBegin)(GLenum1 mode);                    // 开始一次图元，mode 为图元类型
typedef void (GLAPIENTRY *PFN_glEnd)(void);                               // 结束图元并提交
typedef void (GLAPIENTRY *PFN_glVertex2f)(GLfloat1 x, GLfloat1 y);        // 提交二维顶点
typedef void (GLAPIENTRY *PFN_glVertex3f)(GLfloat1 x, GLfloat1 y, GLfloat1 z);  // 提交三维顶点
typedef void (GLAPIENTRY *PFN_glColor3f)(GLfloat1 r, GLfloat1 g, GLfloat1 b);   // 设置当前颜色（无 alpha）
typedef void (GLAPIENTRY *PFN_glColor4f)(GLfloat1 r, GLfloat1 g, GLfloat1 b, GLfloat1 a);  // 设置当前颜色（带 alpha）
typedef void (GLAPIENTRY *PFN_glNormal3f)(GLfloat1 x, GLfloat1 y, GLfloat1 z);  // 提交顶点法线
typedef void (GLAPIENTRY *PFN_glTexCoord2f)(GLfloat1 s, GLfloat1 t);      // 提交纹理坐标
typedef void (GLAPIENTRY *PFN_glMatrixMode)(GLenum1 mode);                // 选择要操作的矩阵栈
typedef void (GLAPIENTRY *PFN_glLoadIdentity)(void);                      // 当前矩阵置为单位阵
typedef void (GLAPIENTRY *PFN_glPushMatrix)(void);                        // 矩阵栈压栈
typedef void (GLAPIENTRY *PFN_glPopMatrix)(void);                         // 矩阵栈出栈
typedef void (GLAPIENTRY *PFN_glOrtho)(GLdouble1 l, GLdouble1 r, GLdouble1 b, GLdouble1 t, GLdouble1 zn, GLdouble1 zf);    // 正交投影矩阵
typedef void (GLAPIENTRY *PFN_glFrustum)(GLdouble1 l, GLdouble1 r, GLdouble1 b, GLdouble1 t, GLdouble1 zn, GLdouble1 zf);  // 透视投影矩阵
typedef void (GLAPIENTRY *PFN_glTranslatef)(GLfloat1 x, GLfloat1 y, GLfloat1 z);  // 当前矩阵乘平移
typedef void (GLAPIENTRY *PFN_glRotatef)(GLfloat1 angle, GLfloat1 x, GLfloat1 y, GLfloat1 z);  // 当前矩阵乘绕轴旋转（角度制）
typedef void (GLAPIENTRY *PFN_glScalef)(GLfloat1 x, GLfloat1 y, GLfloat1 z);      // 当前矩阵乘缩放
typedef void (GLAPIENTRY *PFN_glShadeModel)(GLenum1 mode);                // 设置 SMOOTH/FLAT 着色模型
typedef void (GLAPIENTRY *PFN_glGetFloatv)(GLenum1 pname, GLfloat1* data); // 查询浮点状态（如矩阵、视口）
typedef void (GLAPIENTRY *PFN_glLineWidth)(GLfloat1 w);                   // 线宽（像素）
typedef void (GLAPIENTRY *PFN_glPointSize)(GLfloat1 s);                   // 点大小（像素）
typedef void (GLAPIENTRY *PFN_glLightfv)(GLenum1 light, GLenum1 pname, const GLfloat1* params);     // 设置光源参数
typedef void (GLAPIENTRY *PFN_glMaterialfv)(GLenum1 face, GLenum1 pname, const GLfloat1* params);   // 设置材质参数
typedef void (GLAPIENTRY *PFN_glDepthRange)(GLdouble1 zn, GLdouble1 zf);  // 映射规范化深度到窗口深度区间
typedef void (GLAPIENTRY *PFN_glMultMatrixf)(const GLfloat1* m);          // 当前矩阵乘给定矩阵
typedef void (GLAPIENTRY *PFN_glLoadMatrixf)(const GLfloat1* m);          // 用给定矩阵覆盖当前矩阵

// 固定管线函数地址集合，成员在加载成功后非空，调用前应先用 Gvtcier_Gl1_Available 检查。
struct Gl1 {
    PFN_glBegin glBegin;
    PFN_glEnd glEnd;
    PFN_glVertex2f glVertex2f;
    PFN_glVertex3f glVertex3f;
    PFN_glColor3f glColor3f;
    PFN_glColor4f glColor4f;
    PFN_glNormal3f glNormal3f;
    PFN_glTexCoord2f glTexCoord2f;
    PFN_glMatrixMode glMatrixMode;
    PFN_glLoadIdentity glLoadIdentity;
    PFN_glPushMatrix glPushMatrix;
    PFN_glPopMatrix glPopMatrix;
    PFN_glOrtho glOrtho;
    PFN_glFrustum glFrustum;
    PFN_glTranslatef glTranslatef;
    PFN_glRotatef glRotatef;
    PFN_glScalef glScalef;
    PFN_glShadeModel glShadeModel;
    PFN_glGetFloatv glGetFloatv;
    PFN_glLineWidth glLineWidth;
    PFN_glPointSize glPointSize;
    PFN_glLightfv glLightfv;
    PFN_glMaterialfv glMaterialfv;
    PFN_glDepthRange glDepthRange;
    PFN_glMultMatrixf glMultMatrixf;
    PFN_glLoadMatrixf glLoadMatrixf;
};

// 全局唯一的固定管线函数表实例。
extern Gl1 g;

// 从 opengl32.dll 取全部固定管线函数地址，成功返回 true；已加载过则直接返回 true。
bool Gvtcier_Gl1_LoadFunctions();
// 是否可用：已加载且关键函数（glBegin/glEnd/glVertex3f）都存在；非 Win32 平台恒为 false。
bool Gvtcier_Gl1_Available();

}
