#include "Rhidriver/backend.h"
#include "Rhidriver/sg/mapping_sg.h"
#include "Rhidriver/sg/rhi_sg.h"

namespace rhi {
namespace sg {

void t_Gvtcier_Rhi_GenVertexArrays(int n, Vao* out) {
    for (int i = 0; i < n; ++i) out[i] = NewVao();
}

void t_Gvtcier_Rhi_BindVertexArray(Vao v) { g_state.curVao = v; }

void t_Gvtcier_Rhi_DeleteVertexArrays(int n, const Vao* v) {
    for (int i = 0; i < n; ++i) {
        VaoObj* o = VaoAt(v[i]);
        if (o) o->attribs.clear();
    }
}

void t_Gvtcier_Rhi_VertexAttribPointer(unsigned index, int size, unsigned type, bool normalized,
                                       int stride, const void* ptr) {
    VaoObj* v = VaoAt(g_state.curVao);
    if (!v) return;
    AttribDesc a;
    a.index = index;
    a.size = size;
    a.type = type;
    a.normalized = normalized;
    a.stride = stride;
    a.offset = (uint64_t)(uintptr_t)ptr;
    for (AttribDesc& e : v->attribs) {
        if (e.index == index) {
            a.divisor = e.divisor;
            e = a;
            return;
        }
    }
    v->attribs.push_back(a);
}

void t_Gvtcier_Rhi_EnableAttrib(unsigned index) { (void)index; }

void t_Gvtcier_Rhi_VertexAttribDivisor(unsigned index, unsigned divisor) {
    VaoObj* v = VaoAt(g_state.curVao);
    if (!v) return;
    for (AttribDesc& e : v->attribs) {
        if (e.index == index) {
            e.divisor = divisor;
            return;
        }
    }
}

}
}
