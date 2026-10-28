#include "Gvtcier.h"
#include <cmath>

//网格法线相关的几何工具，主要用于导入外部模型后修正朝向：
//外部资源常出现整体法线朝内或三角形绕序反了的问题，表现为模型发黑或只看到内壁。

namespace GvtcierNormal {

//由三个顶点求面法线（未归一化）。
//叉积的模长正比于三角形面积，因此后面累加时面积大的三角形自然权重更高，无需额外加权。
Vec3 faceNormal(const Vec3& a, const Vec3& b, const Vec3& c) {
    return (b - a).cross(c - a);
}

//安全归一化：长度过小时返回 fallback。
//退化三角形（三点共线或重合）的面法线接近零向量，直接归一化会产生 NaN 或除零，必须兜底。
Vec3 safeNormalize(const Vec3& v, const Vec3& fallback) {
    float len = v.length();
    return len > 1e-9f ? v * (1.0f / len) : fallback;
}

//用带符号体积判断网格的三角形绕序是否与期望相反。
//原理：把每个三角形与原点构成四面体求有符号体积并累加（散度定理），
//封闭网格的总体积符号取决于绕序：正值说明外法线朝外，负值说明绕序反了。
//非封闭网格（如单个平面）结果没有几何意义，因此只对封闭模型可靠。
//verts 为顶点数组，indices 为三角形索引；越界索引被跳过而不是崩溃。
float signedVolume(const std::vector<Vertex>& verts, const std::vector<unsigned>& indices) {
    double sum = 0.0;
    for (size_t k = 0; k + 2 < indices.size(); k += 3) {
        unsigned i0 = indices[k], i1 = indices[k + 1], i2 = indices[k + 2];
        if (i0 >= verts.size() || i1 >= verts.size() || i2 >= verts.size()) continue;
        const Vec3& a = verts[i0].pos;
        const Vec3& b = verts[i1].pos;
        const Vec3& c = verts[i2].pos;
        sum += (double)a.x * ((double)b.y * c.z - (double)b.z * c.y)
             - (double)a.y * ((double)b.x * c.z - (double)b.z * c.x)
             + (double)a.z * ((double)b.x * c.y - (double)b.y * c.x);
    }
    return (float)(sum / 6.0);
}

//是否需要翻转绕序：带符号体积为负即表示三角形顺序反了。把几何分析结论直接翻译成布尔判断。
bool needsWindingFlip(const std::vector<Vertex>& verts, const std::vector<unsigned>& indices) {
    return signedVolume(verts, indices) < 0.0f;
}

//就地翻转每个三角形的绕序：交换后两个索引即可，顶点数据不用动。
//相比重建索引，这种做法几乎零开销，也保留了原有的顶点复用关系。
void flipWinding(std::vector<unsigned>& indices) {
    for (size_t k = 0; k + 2 < indices.size(); k += 3) {
        unsigned t = indices[k + 1];
        indices[k + 1] = indices[k + 2];
        indices[k + 2] = t;
    }
}

//翻转所有顶点法线（同时处理硬边与平滑法线两套数据）。取反而不是重新计算，保留原有朝向的细节。
void flipNormals(std::vector<Vertex>& verts) {
    for (size_t i = 0; i < verts.size(); ++i) {
        verts[i].normal = verts[i].normal * -1.0f;
        verts[i].smoothNormal = verts[i].smoothNormal * -1.0f;
    }
}

//按面法线累加重算顶点法线：先把所有顶点法线清零，再把每个三角形的面法线加给它的三个顶点。
//累加而不平均就得到面积加权效果（模长为两倍面积的叉积），大三角形对结果贡献更大，视觉上更自然。
//因为共用一个索引被多个面引用的顶点会累加多个面法线，结果天然是平滑着色；
//需要硬边时应先按面拆分顶点，否则所有边都会被平滑掉。
void accumulateFaceNormals(std::vector<Vertex>& verts, const std::vector<unsigned>& indices) {
    for (size_t i = 0; i < verts.size(); ++i) verts[i].normal = Vec3(0, 0, 0);
    for (size_t k = 0; k + 2 < indices.size(); k += 3) {
        unsigned i0 = indices[k], i1 = indices[k + 1], i2 = indices[k + 2];
        if (i0 >= verts.size() || i1 >= verts.size() || i2 >= verts.size()) continue;
        Vec3 fn = faceNormal(verts[i0].pos, verts[i1].pos, verts[i2].pos);
        verts[i0].normal = verts[i0].normal + fn;
        verts[i1].normal = verts[i1].normal + fn;
        verts[i2].normal = verts[i2].normal + fn;
    }
}

//把所有顶点法线归一化。累加后长度不为 1，必须归一化才能用于光照；
//孤立顶点（未被任何三角形引用）累加结果为零向量，safeNormalize 会返回其默认的兜底方向，不会产生 NaN。
void normalizeVertNormals(std::vector<Vertex>& verts) {
    for (size_t i = 0; i < verts.size(); ++i)
        verts[i].normal = safeNormalize(verts[i].normal);
}

}
