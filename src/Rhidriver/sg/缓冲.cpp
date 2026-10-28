#include "Rhidriver/backend.h"
#include "Rhidriver/sg/rhi_sg.h"

#include <cstring>

namespace rhi {
namespace sg {

void t_Gvtcier_Rhi_GenBuffers(int n, Buffer* out) {
    for (int i = 0; i < n; ++i) out[i] = NewBuffer();
}

void t_Gvtcier_Rhi_BindBuffer(unsigned target, Buffer b) {
    SetCurrentBuffer(target, b);
    BufferObj* o = BufferAt(b);
    if (o) o->target = target;
}

// 引擎每次 BufferData 都当全新的内容，sokol 的缓冲建好后不能改大小，
// 所以整块重建最省心，顺带把旧缓冲释放掉。
void t_Gvtcier_Rhi_BufferData(unsigned target, size_t size, const void* data, unsigned usage) {
    BufferObj* o = BufferAt(CurrentBuffer(target));
    if (!o) return;
    if (!sg_isvalid()) return;

    if (o->buf.id != SG_INVALID_ID) {
        sg_destroy_buffer(o->buf);
        o->buf = sg_buffer{};
    }

    size_t need = size ? size : 4;
    bool dynamic = (usage != StaticDraw);

    sg_buffer_desc bd{};
    bd.size = need;
    bd.usage.vertex_buffer = (target == ArrayBuffer);
    bd.usage.index_buffer = (target == ElementArrayBuffer);
    bd.usage.storage_buffer = (target == ShaderStorageBuffer);
    bd.usage.write_transient = dynamic;
    if (data && !dynamic) {
        bd.data.ptr = data;
        bd.data.size = need;
    }

    o->buf = sg_make_buffer(&bd);
    o->target = target;
    o->size = need;
    o->dynamic = dynamic;

    if (data && dynamic && o->buf.id != SG_INVALID_ID) {
        sg_write_buffer_desc wd{};
        wd.src.data.ptr = data;
        wd.src.data.size = need;
        wd.dst.buffer = o->buf;
        wd.dst.offset = 0;
        wd.size = need;
        sg_write_buffer_transient(&wd);
    }
}

void t_Gvtcier_Rhi_BufferSubData(unsigned target, ptrdiff_t offset, size_t size, const void* data) {
    BufferObj* o = BufferAt(CurrentBuffer(target));
    if (!o || !data || o->buf.id == SG_INVALID_ID) return;
    if (offset < 0 || (size_t)offset + size > o->size) return;
    sg_write_buffer_desc wd{};
    wd.src.data.ptr = data;
    wd.src.data.size = size;
    wd.src.offset = 0;
    wd.dst.buffer = o->buf;
    wd.dst.offset = (size_t)offset;
    wd.size = size;
    sg_write_buffer_transient(&wd);
}

// sokol 的绑定走 apply_bindings，所以这里只记下来，绘制时一起提交
void t_Gvtcier_Rhi_BindBufferBase(unsigned target, unsigned index, Buffer b) {
    (void)target;
    if (index >= 16) return;
    ProgramObj* p = ProgramAt(g_state.curProg);
    if (p) p->ssbo[index] = b;
}

void t_Gvtcier_Rhi_DeleteBuffers(int n, const Buffer* b) {
    for (int i = 0; i < n; ++i) {
        BufferObj* o = BufferAt(b[i]);
        if (!o) continue;
        if (o->buf.id != SG_INVALID_ID) {
            sg_destroy_buffer(o->buf);
            o->buf = sg_buffer{};
        }
        o->size = 0;
    }
}

}
}
