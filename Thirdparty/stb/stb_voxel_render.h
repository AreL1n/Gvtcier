#ifndef INCLUDE_STB_VOXEL_RENDER_H
#define INCLUDE_STB_VOXEL_RENDER_H

#include <stdlib.h>

typedef struct stbvox_mesh_maker stbvox_mesh_maker;
typedef struct stbvox_input_description stbvox_input_description;

#ifdef STB_VOXEL_RENDER_STATIC
#define STBVXDEC static
#else
#define STBVXDEC extern
#endif

#ifdef __cplusplus
extern "C" {
#endif

STBVXDEC void stbvox_init_mesh_maker(stbvox_mesh_maker *mm);

STBVXDEC void stbvox_set_buffer(stbvox_mesh_maker *mm, int mesh, int slot, void *buffer, size_t len);

STBVXDEC int stbvox_get_buffer_count(stbvox_mesh_maker *mm);

STBVXDEC int stbvox_get_buffer_size_per_quad(stbvox_mesh_maker *mm, int slot);

STBVXDEC void stbvox_set_default_mesh(stbvox_mesh_maker *mm, int mesh);

STBVXDEC stbvox_input_description *stbvox_get_input_description(stbvox_mesh_maker *mm);

STBVXDEC void stbvox_set_input_stride(stbvox_mesh_maker *mm, int x_stride_in_elements, int y_stride_in_elements);

STBVXDEC void stbvox_set_input_range(stbvox_mesh_maker *mm, int x0, int y0, int z0, int x1, int y1, int z1);

STBVXDEC int stbvox_make_mesh(stbvox_mesh_maker *mm);

STBVXDEC int stbvox_get_quad_count(stbvox_mesh_maker *mm, int mesh);

STBVXDEC void stbvox_set_mesh_coordinates(stbvox_mesh_maker *mm, int x, int y, int z);

STBVXDEC void stbvox_get_bounds(stbvox_mesh_maker *mm, float bounds[2][3]);

STBVXDEC void stbvox_get_transform(stbvox_mesh_maker *mm, float transform[3][3]);

STBVXDEC void stbvox_reset_buffers(stbvox_mesh_maker *mm);

STBVXDEC char *stbvox_get_vertex_shader(void);

STBVXDEC char *stbvox_get_fragment_shader(void);

STBVXDEC char *stbvox_get_fragment_shader_alpha_only(void);

typedef struct stbvox_uniform_info stbvox_uniform_info;

STBVXDEC int stbvox_get_uniform_info(stbvox_uniform_info *info, int uniform);

enum                           
{                              
   STBVOX_UNIFORM_face_data,   
   STBVOX_UNIFORM_transform,   
   STBVOX_UNIFORM_tex_array,   
   STBVOX_UNIFORM_texscale,    
   STBVOX_UNIFORM_color_table, 
   STBVOX_UNIFORM_normals,     
   STBVOX_UNIFORM_texgen,      
   STBVOX_UNIFORM_ambient,     
   STBVOX_UNIFORM_camera_pos,  

   STBVOX_UNIFORM_count,
};

enum
{
   STBVOX_UNIFORM_TYPE_none,
   STBVOX_UNIFORM_TYPE_sampler,
   STBVOX_UNIFORM_TYPE_vec2,
   STBVOX_UNIFORM_TYPE_vec3,
   STBVOX_UNIFORM_TYPE_vec4,
};

struct stbvox_uniform_info
{
   int type;                    
   int bytes_per_element;       
   int array_length;            
   char *name;                  
   float *default_value;        
   int use_tex_buffer;          
};

#if 0

void setup_uniforms(GLuint shader, float camera_pos[4], GLuint tex1, GLuint tex2)
{
   int i;
   glUseProgram(shader); 
   for (i=0; i < STBVOX_UNIFORM_count; ++i) {
      stbvox_uniform_info sui;
      if (stbvox_get_uniform_info(&sui, i)) {
         GLint loc = glGetUniformLocation(shader, sui.name);
         if (loc != -1) {
            switch (i) {
               case STBVOX_UNIFORM_camera_pos: 
                  glUniform4fv(loc, sui.array_length, camera_pos);
                  break;

               case STBVOX_UNIFORM_tex_array: {
                  GLuint tex_unit[2] = { 0, 1 }; 
                  glUniform1iv(loc, 2, tex_unit);

                  glActiveTexture(GL_TEXTURE0 + tex_unit[0]); glBindTexture(GL_TEXTURE_2D_ARRAY, tex1);
                  glActiveTexture(GL_TEXTURE0 + tex_unit[1]); glBindTexture(GL_TEXTURE_2D_ARRAY, tex2);
                  glActiveTexture(GL_TEXTURE0); 
                  break;
               }

               case STBVOX_UNIFORM_face_data:
                  glUniform1i(loc, SAMPLER_YOU_WILL_BIND_PER_MESH_FACE_DATA_TO);
                  break;

               case STBVOX_UNIFORM_ambient:     
               case STBVOX_UNIFORM_color_table: 
               case STBVOX_UNIFORM_texscale:    
                  glUniform4fv(loc, sui.array_length, sui.default_value);
                  break;

               case STBVOX_UNIFORM_normals:     
               case STBVOX_UNIFORM_texgen:      
                  glUniform3fv(loc, sui.array_length, sui.default_value);
                  break;
            }
         }
      }
   }
}
#endif

#ifdef __cplusplus
}
#endif

enum
{
   STBVOX_GEOM_empty,
   STBVOX_GEOM_knockout,  
   STBVOX_GEOM_solid,
   STBVOX_GEOM_transp,    

   STBVOX_GEOM_slab_upper,
   STBVOX_GEOM_slab_lower,
   STBVOX_GEOM_floor_slope_north_is_top,
   STBVOX_GEOM_ceil_slope_north_is_bottom,

   STBVOX_GEOM_floor_slope_north_is_top_as_wall_UNIMPLEMENTED,   
   STBVOX_GEOM_ceil_slope_north_is_bottom_as_wall_UNIMPLEMENTED,
   STBVOX_GEOM_crossed_pair,    
   STBVOX_GEOM_force,           

   STBVOX_GEOM_floor_vheight_03 = 12,  
   STBVOX_GEOM_floor_vheight_12,       
   STBVOX_GEOM_ceil_vheight_03,
   STBVOX_GEOM_ceil_vheight_12,

   STBVOX_GEOM_count, 
};

enum
{
   STBVOX_FACE_east,
   STBVOX_FACE_north,
   STBVOX_FACE_west,
   STBVOX_FACE_south,
   STBVOX_FACE_up,
   STBVOX_FACE_down,

   STBVOX_FACE_count,
};

#ifdef STBVOX_CONFIG_BLOCKTYPE_SHORT
typedef unsigned short stbvox_block_type;
#else
typedef unsigned char stbvox_block_type;
#endif

typedef struct
{
   unsigned char r,g,b;
} stbvox_rgb;

#define STBVOX_COLOR_TEX1_ENABLE   64
#define STBVOX_COLOR_TEX2_ENABLE  128

struct stbvox_input_description
{
   unsigned char lighting_at_vertices;

   stbvox_rgb *rgb;

   unsigned char *lighting;

   stbvox_block_type *blocktype;

   unsigned char *geometry;

   unsigned char *block_geometry;

   unsigned char *block_tex1;

   unsigned char (*block_tex1_face)[6];

   unsigned char *tex2;

   unsigned char *block_tex2;

   unsigned char (*block_tex2_face)[6];

   unsigned char *color;

   unsigned char *block_color;

   unsigned char (*block_color_face)[6];

   unsigned char *block_texlerp;

   unsigned char (*block_texlerp_face)[6];

   unsigned char *block_vheight;

   unsigned char *selector;

   unsigned char *block_selector;

   unsigned char *side_texrot;

   unsigned char *block_side_texrot;

   unsigned char *overlay;                 

   unsigned char (*overlay_tex1)[6];

   unsigned char (*overlay_tex2)[6];

   unsigned char (*overlay_color)[6];

   unsigned char *overlay_side_texrot;

   unsigned char *rotate;

   unsigned char *tex2_for_tex1;

   unsigned char *tex2_replace;

   unsigned char *tex2_facemask;

   unsigned char *extended_color;

   unsigned char *ecolor_color;

   unsigned char *ecolor_facemask;

   unsigned char *color2;

   unsigned char *color2_facemask;

   unsigned char *color3;

   unsigned char *color3_facemask;

   unsigned char *texlerp_simple;

   unsigned char *texlerp;

   unsigned short *texlerp_vert3;

   unsigned short *texlerp_face3;          

   unsigned char *vheight;                 

   unsigned char *packed_compact;

};

enum
{
   STBVOX_VERTEX_HEIGHT_0,
   STBVOX_VERTEX_HEIGHT_half,
   STBVOX_VERTEX_HEIGHT_1,
   STBVOX_VERTEX_HEIGHT_one_and_a_half,
};

enum
{
   STBVOX_TEXLERP_FACE_0,
   STBVOX_TEXLERP_FACE_half,
   STBVOX_TEXLERP_FACE_1,
   STBVOX_TEXLERP_FACE_use_vert,
};

enum
{
   STBVOX_TEXLERP_BASE_0,    
   STBVOX_TEXLERP_BASE_2_7,  
   STBVOX_TEXLERP_BASE_5_7,  
   STBVOX_TEXLERP_BASE_1     
};

enum
{
   STBVOX_TEXLERP3_0_8,
   STBVOX_TEXLERP3_1_8,
   STBVOX_TEXLERP3_2_8,
   STBVOX_TEXLERP3_3_8,
   STBVOX_TEXLERP3_4_8,
   STBVOX_TEXLERP3_5_8,
   STBVOX_TEXLERP3_6_8,
   STBVOX_TEXLERP3_7_8,
};

#define STBVOX_FACE_NONE  7

#define STBVOX_BLOCKTYPE_EMPTY    0

#ifdef STBVOX_BLOCKTYPE_SHORT
#define STBVOX_BLOCKTYPE_HOLE  65535
#else
#define STBVOX_BLOCKTYPE_HOLE    255
#endif

#define STBVOX_MAKE_GEOMETRY(geom, rotate, vheight) ((geom) + (rotate)*16 + (vheight)*64)
#define STBVOX_MAKE_VHEIGHT(v_sw, v_se, v_nw, v_ne) ((v_sw) + (v_se)*4 + (v_nw)*16 + (v_ne)*64)
#define STBVOX_MAKE_MATROT(block, overlay, color)  ((block) + (overlay)*4 + (color)*64)
#define STBVOX_MAKE_TEX2_REPLACE(tex2, tex2_replace_face) ((tex2) + ((tex2_replace_face) & 3)*64)
#define STBVOX_MAKE_TEXLERP(ns2, ew2, ud2, vert)  ((ew2) + (ns2)*4 + (ud2)*16 + (vert)*64)
#define STBVOX_MAKE_TEXLERP_SIMPLE(baselerp,vert,face)   ((vert)*32 + (face)*4 + (baselerp))
#define STBVOX_MAKE_TEXLERP1(vert,e2,n2,w2,s2,u4,d2) STBVOX_MAKE_TEXLERP(s2, w2, d2, vert)
#define STBVOX_MAKE_TEXLERP2(vert,e2,n2,w2,s2,u4,d2) ((u2)*16 + (n2)*4 + (s2))
#define STBVOX_MAKE_FACE_MASK(e,n,w,s,u,d)  ((e)+(n)*2+(w)*4+(s)*8+(u)*16+(d)*32)
#define STBVOX_MAKE_SIDE_TEXROT(e,n,w,s) ((e)+(n)*4+(w)*16+(s)*64)
#define STBVOX_MAKE_COLOR(color,t1,t2) ((color)+(t1)*64+(t2)*128)
#define STBVOX_MAKE_TEXLERP_VERT3(e,n,w,s,u)   ((e)+(n)*8+(w)*64+(s)*512+(u)*4096)
#define STBVOX_MAKE_TEXLERP_FACE3(e,n,w,s,u,d) ((e)+(n)*8+(w)*64+(s)*512+(u)*4096+(d)*16384)
#define STBVOX_MAKE_PACKED_COMPACT(rot, vheight, texlerp, def) ((rot)+4*(vheight)+16*(use)+32*(texlerp))

#define STBVOX_MAKE_LIGHTING_EXT(lighting, rot)  (((lighting)&~3)+(rot))
#define STBVOX_MAKE_LIGHTING(lighting)       (lighting)

#ifndef STBVOX_MAX_MESHES
#define STBVOX_MAX_MESHES      2           
#endif

#define STBVOX_MAX_MESH_SLOTS  3           

struct stbvox_mesh_maker
{
   stbvox_input_description input;
   int cur_x, cur_y, cur_z;       
   int x0,y0,z0,x1,y1,z1;
   int x_stride_in_bytes;
   int y_stride_in_bytes;
   int config_dirty;
   int default_mesh;
   unsigned int tags;

   int cube_vertex_offset[6][4]; 
   int vertex_gather_offset[6][4];

   int pos_x,pos_y,pos_z;
   int full;

   char *output_cur   [STBVOX_MAX_MESHES][STBVOX_MAX_MESH_SLOTS];
   char *output_end   [STBVOX_MAX_MESHES][STBVOX_MAX_MESH_SLOTS];
   char *output_buffer[STBVOX_MAX_MESHES][STBVOX_MAX_MESH_SLOTS];
   int   output_len   [STBVOX_MAX_MESHES][STBVOX_MAX_MESH_SLOTS];

   int   output_size  [STBVOX_MAX_MESHES][STBVOX_MAX_MESH_SLOTS]; 
   int   output_step  [STBVOX_MAX_MESHES][STBVOX_MAX_MESH_SLOTS]; 
   int   num_mesh_slots;

   float default_tex_scale[128][2];
};

#endif 

#ifdef STB_VOXEL_RENDER_IMPLEMENTATION

#include <stdlib.h>
#include <assert.h>
#include <string.h> 

#ifndef _MSC_VER
   #include <stdint.h>
   typedef uint16_t stbvox_uint16;
   typedef uint32_t stbvox_uint32;
#else
   typedef unsigned short stbvox_uint16;
   typedef unsigned int   stbvox_uint32;
#endif

#ifdef _MSC_VER
   #define STBVOX_NOTUSED(v)  (void)(v)
#else
   #define STBVOX_NOTUSED(v)  (void)sizeof(v)
#endif

#ifndef STBVOX_CONFIG_MODE
#error "Must defined STBVOX_CONFIG_MODE to select the mode"
#endif

#if defined(STBVOX_CONFIG_ROTATION_IN_LIGHTING) && defined(STBVOX_CONFIG_VHEIGHT_IN_LIGHTING)
#error "Can't store both rotation and vheight in lighting"
#endif

#if STBVOX_CONFIG_MODE==0 || STBVOX_CONFIG_MODE==1

   #define STBVOX_ICONFIG_VERTEX_32
   #define STBVOX_ICONFIG_FACE1_1

#elif STBVOX_CONFIG_MODE==20 || STBVOX_CONFIG_MODE==21

   #define STBVOX_ICONFIG_VERTEX_32
   #define STBVOX_ICONFIG_FACE1_1
   #define STBVOX_ICONFIG_UNTEXTURED

#else
#error "Selected value of STBVOX_CONFIG_MODE is not supported"
#endif

#if STBVOX_CONFIG_MODE==0 || STBVOX_CONFIG_MODE==20
#define STBVOX_ICONFIG_FACE_ATTRIBUTE
#endif

#ifndef STBVOX_CONFIG_HLSL

#define STBVOX_ICONFIG_GLSL
#endif

#ifdef STBVOX_CONFIG_OPENGL_MODELVIEW
#define STBVOX_ICONFIG_OPENGL_3_1_COMPATIBILITY
#endif

#if defined(STBVOX_ICONFIG_VERTEX_32)
   typedef stbvox_uint32 stbvox_mesh_vertex;
   #define stbvox_vertex_encode(x,y,z,ao,texlerp) \
      ((stbvox_uint32) ((x)+((y)<<7)+((z)<<14)+((ao)<<23)+((texlerp)<<29)))
#elif defined(STBVOX_ICONFIG_VERTEX_16_1)  
   typedef stbvox_uint16 stbvox_mesh_vertex;
   #define stbvox_vertex_encode(x,y,z,ao,texlerp) \
      ((stbvox_uint16) ((z)+((ao)<<7)+((texlerp)<<13)
#elif defined(STBVOX_ICONFIG_VERTEX_16_2)  
   typedef stbvox_uint16 stbvox_mesh_vertex;
   #define stbvox_vertex_encode(x,y,z,ao,texlerp) \
      ((stbvox_uint16) ((x)+((z)<<6))+((ao)<<10))
#elif defined(STBVOX_ICONFIG_VERTEX_8)
   typedef stbvox_uint8 stbvox_mesh_vertex;
   #define stbvox_vertex_encode(x,y,z,ao,texlerp) \
      ((stbvox_uint8) ((z)+((ao)<<6))
#else
   #error "internal error, no vertex type"
#endif

#ifdef STBVOX_ICONFIG_FACE1_1
   typedef struct
   {
      unsigned char tex1,tex2,color,face_info;
   } stbvox_mesh_face;
#else
   #error "internal error, no face type"
#endif

enum
{
   STBVF_e,
   STBVF_n,
   STBVF_w,
   STBVF_s,
   STBVF_u,
   STBVF_d,
   STBVF_eu,
   STBVF_ed,

   STBVF_eu_wall,
   STBVF_nu_wall,
   STBVF_wu_wall,
   STBVF_su_wall,
   STBVF_ne_u,
   STBVF_ne_d,
   STBVF_nu,
   STBVF_nd,

   STBVF_ed_wall,
   STBVF_nd_wall,
   STBVF_wd_wall,
   STBVF_sd_wall,
   STBVF_nw_u,
   STBVF_nw_d,
   STBVF_wu,
   STBVF_wd,

   STBVF_ne_u_cross,
   STBVF_nw_u_cross,
   STBVF_sw_u_cross,
   STBVF_se_u_cross,
   STBVF_sw_u,
   STBVF_sw_d,
   STBVF_su,
   STBVF_sd,

   STBVF_se_u = STBVF_su,
   STBVF_se_d = STBVF_sd,

   STBVF_count,
};

static float stbvox_default_texgen[2][32][3] =
{
   { {  0, 1,0 }, { 0, 0, 1 }, {  0,-1,0 }, { 0, 0,-1 },
     { -1, 0,0 }, { 0, 0, 1 }, {  1, 0,0 }, { 0, 0,-1 },
     {  0,-1,0 }, { 0, 0, 1 }, {  0, 1,0 }, { 0, 0,-1 },
     {  1, 0,0 }, { 0, 0, 1 }, { -1, 0,0 }, { 0, 0,-1 },

     {  1, 0,0 }, { 0, 1, 0 }, { -1, 0,0 }, { 0,-1, 0 },
     { -1, 0,0 }, { 0,-1, 0 }, {  1, 0,0 }, { 0, 1, 0 },
     {  1, 0,0 }, { 0, 1, 0 }, { -1, 0,0 }, { 0,-1, 0 },
     { -1, 0,0 }, { 0,-1, 0 }, {  1, 0,0 }, { 0, 1, 0 },
   },
   { { 0, 0,-1 }, {  0, 1,0 }, { 0, 0, 1 }, {  0,-1,0 },
     { 0, 0,-1 }, { -1, 0,0 }, { 0, 0, 1 }, {  1, 0,0 },
     { 0, 0,-1 }, {  0,-1,0 }, { 0, 0, 1 }, {  0, 1,0 },
     { 0, 0,-1 }, {  1, 0,0 }, { 0, 0, 1 }, { -1, 0,0 },

     { 0,-1, 0 }, {  1, 0,0 }, { 0, 1, 0 }, { -1, 0,0 },
     { 0, 1, 0 }, { -1, 0,0 }, { 0,-1, 0 }, {  1, 0,0 },
     { 0,-1, 0 }, {  1, 0,0 }, { 0, 1, 0 }, { -1, 0,0 },
     { 0, 1, 0 }, { -1, 0,0 }, { 0,-1, 0 }, {  1, 0,0 },
   },
};

#define STBVOX_RSQRT2   0.7071067811865f
#define STBVOX_RSQRT3   0.5773502691896f

static float stbvox_default_normals[32][3] =
{
   { 1,0,0 },  
   { 0,1,0 },  
   { -1,0,0 }, 
   { 0,-1,0 }, 
   { 0,0,1 },  
   { 0,0,-1 }, 
   {  STBVOX_RSQRT2,0, STBVOX_RSQRT2 }, 
   {  STBVOX_RSQRT2,0, -STBVOX_RSQRT2 }, 

   {  STBVOX_RSQRT2,0, STBVOX_RSQRT2 }, 
   { 0, STBVOX_RSQRT2, STBVOX_RSQRT2 }, 
   { -STBVOX_RSQRT2,0, STBVOX_RSQRT2 }, 
   { 0,-STBVOX_RSQRT2, STBVOX_RSQRT2 }, 
   {  STBVOX_RSQRT3, STBVOX_RSQRT3, STBVOX_RSQRT3 }, 
   {  STBVOX_RSQRT3, STBVOX_RSQRT3,-STBVOX_RSQRT3 }, 
   { 0, STBVOX_RSQRT2, STBVOX_RSQRT2 }, 
   { 0, STBVOX_RSQRT2, -STBVOX_RSQRT2 }, 

   {  STBVOX_RSQRT2,0, -STBVOX_RSQRT2 }, 
   { 0, STBVOX_RSQRT2, -STBVOX_RSQRT2 }, 
   { -STBVOX_RSQRT2,0, -STBVOX_RSQRT2 }, 
   { 0,-STBVOX_RSQRT2, -STBVOX_RSQRT2 }, 
   { -STBVOX_RSQRT3, STBVOX_RSQRT3, STBVOX_RSQRT3 }, 
   { -STBVOX_RSQRT3, STBVOX_RSQRT3,-STBVOX_RSQRT3 }, 
   { -STBVOX_RSQRT2,0, STBVOX_RSQRT2 }, 
   { -STBVOX_RSQRT2,0, -STBVOX_RSQRT2 }, 

   {  STBVOX_RSQRT3, STBVOX_RSQRT3,STBVOX_RSQRT3 }, 
   { -STBVOX_RSQRT3, STBVOX_RSQRT3,STBVOX_RSQRT3 }, 
   { -STBVOX_RSQRT3,-STBVOX_RSQRT3,STBVOX_RSQRT3 }, 
   {  STBVOX_RSQRT3,-STBVOX_RSQRT3,STBVOX_RSQRT3 }, 
   { -STBVOX_RSQRT3,-STBVOX_RSQRT3, STBVOX_RSQRT3 }, 
   { -STBVOX_RSQRT3,-STBVOX_RSQRT3,-STBVOX_RSQRT3 }, 
   { 0,-STBVOX_RSQRT2, STBVOX_RSQRT2 }, 
   { 0,-STBVOX_RSQRT2, -STBVOX_RSQRT2 }, 
};

static float stbvox_default_texscale[128][4] =
{
   {1,1,0,0},{1,1,0,0},{1,1,0,0},{1,1,0,0},{1,1,0,0},{1,1,0,0},{1,1,0,0},{1,1,0,0},
   {1,1,0,0},{1,1,0,0},{1,1,0,0},{1,1,0,0},{1,1,0,0},{1,1,0,0},{1,1,0,0},{1,1,0,0},
   {1,1,0,0},{1,1,0,0},{1,1,0,0},{1,1,0,0},{1,1,0,0},{1,1,0,0},{1,1,0,0},{1,1,0,0},
   {1,1,0,0},{1,1,0,0},{1,1,0,0},{1,1,0,0},{1,1,0,0},{1,1,0,0},{1,1,0,0},{1,1,0,0},
   {1,1,0,0},{1,1,0,0},{1,1,0,0},{1,1,0,0},{1,1,0,0},{1,1,0,0},{1,1,0,0},{1,1,0,0},
   {1,1,0,0},{1,1,0,0},{1,1,0,0},{1,1,0,0},{1,1,0,0},{1,1,0,0},{1,1,0,0},{1,1,0,0},
   {1,1,0,0},{1,1,0,0},{1,1,0,0},{1,1,0,0},{1,1,0,0},{1,1,0,0},{1,1,0,0},{1,1,0,0},
   {1,1,0,0},{1,1,0,0},{1,1,0,0},{1,1,0,0},{1,1,0,0},{1,1,0,0},{1,1,0,0},{1,1,0,0},
   {1,1,0,0},{1,1,0,0},{1,1,0,0},{1,1,0,0},{1,1,0,0},{1,1,0,0},{1,1,0,0},{1,1,0,0},
   {1,1,0,0},{1,1,0,0},{1,1,0,0},{1,1,0,0},{1,1,0,0},{1,1,0,0},{1,1,0,0},{1,1,0,0},
   {1,1,0,0},{1,1,0,0},{1,1,0,0},{1,1,0,0},{1,1,0,0},{1,1,0,0},{1,1,0,0},{1,1,0,0},
   {1,1,0,0},{1,1,0,0},{1,1,0,0},{1,1,0,0},{1,1,0,0},{1,1,0,0},{1,1,0,0},{1,1,0,0},
   {1,1,0,0},{1,1,0,0},{1,1,0,0},{1,1,0,0},{1,1,0,0},{1,1,0,0},{1,1,0,0},{1,1,0,0},
   {1,1,0,0},{1,1,0,0},{1,1,0,0},{1,1,0,0},{1,1,0,0},{1,1,0,0},{1,1,0,0},{1,1,0,0},
   {1,1,0,0},{1,1,0,0},{1,1,0,0},{1,1,0,0},{1,1,0,0},{1,1,0,0},{1,1,0,0},{1,1,0,0},
   {1,1,0,0},{1,1,0,0},{1,1,0,0},{1,1,0,0},{1,1,0,0},{1,1,0,0},{1,1,0,0},{1,1,0,0},
};

static unsigned char stbvox_default_palette_compact[64][3] =
{
   { 255,255,255 }, { 238,238,238 }, { 221,221,221 }, { 204,204,204 },
   { 187,187,187 }, { 170,170,170 }, { 153,153,153 }, { 136,136,136 },
   { 119,119,119 }, { 102,102,102 }, {  85, 85, 85 }, {  68, 68, 68 },
   {  51, 51, 51 }, {  34, 34, 34 }, {  17, 17, 17 }, {   0,  0,  0 },
   { 255,240,240 }, { 255,220,220 }, { 255,160,160 }, { 255, 32, 32 },
   { 200,120,160 }, { 200, 60,150 }, { 220,100,130 }, { 255,  0,128 },
   { 240,240,255 }, { 220,220,255 }, { 160,160,255 }, {  32, 32,255 },
   { 120,160,200 }, {  60,150,200 }, { 100,130,220 }, {   0,128,255 },
   { 240,255,240 }, { 220,255,220 }, { 160,255,160 }, {  32,255, 32 },
   { 160,200,120 }, { 150,200, 60 }, { 130,220,100 }, { 128,255,  0 },
   { 255,255,240 }, { 255,255,220 }, { 220,220,180 }, { 255,255, 32 },
   { 200,160,120 }, { 200,150, 60 }, { 220,130,100 }, { 255,128,  0 },
   { 255,240,255 }, { 255,220,255 }, { 220,180,220 }, { 255, 32,255 },
   { 160,120,200 }, { 150, 60,200 }, { 130,100,220 }, { 128,  0,255 },
   { 240,255,255 }, { 220,255,255 }, { 180,220,220 }, {  32,255,255 },
   { 120,200,160 }, {  60,200,150 }, { 100,220,130 }, {   0,255,128 },
};

static float stbvox_default_ambient[4][4] =
{
   { 0,0,1      ,0 }, 
   { 0.5,0.5,0.5,0 }, 
   { 0.5,0.5,0.5,0 }, 
   { 0.5,0.5,0.5,1.0f/1000.0f/1000.0f }, 
};

static float stbvox_default_palette[64][4];

static void stbvox_build_default_palette(void)
{
   int i;
   for (i=0; i < 64; ++i) {
      stbvox_default_palette[i][0] = stbvox_default_palette_compact[i][0] / 255.0f;
      stbvox_default_palette[i][1] = stbvox_default_palette_compact[i][1] / 255.0f;
      stbvox_default_palette[i][2] = stbvox_default_palette_compact[i][2] / 255.0f;
      stbvox_default_palette[i][3] = 1.0f;
   }
}

#if defined(STBVOX_ICONFIG_OPENGL_3_1_COMPATIBILITY)
   #define STBVOX_SHADER_VERSION "#version 150 compatibility\n"
#elif defined(STBVOX_ICONFIG_OPENGL_3_0)
   #define STBVOX_SHADER_VERSION "#version 130\n"
#elif defined(STBVOX_ICONFIG_GLSL)
   #define STBVOX_SHADER_VERSION "#version 150\n"
#else
   #define STBVOX_SHADER_VERSION ""
#endif

static const char *stbvox_vertex_program =
{
      STBVOX_SHADER_VERSION

   #ifdef STBVOX_ICONFIG_FACE_ATTRIBUTE  
      "in uvec4 attr_face;\n"
   #else
      "uniform usamplerBuffer facearray;\n"
   #endif

   #ifdef STBVOX_ICONFIG_FACE_ARRAY_2
      "uniform usamplerBuffer facearray2;\n"
   #endif

      "in uint attr_vertex;\n"

      "uniform vec3 transform[3];\n"

      "uniform vec4 camera_pos;\n"  

      "uniform vec3 normal_table[32];\n"

      #ifndef STBVOX_CONFIG_OPENGL_MODELVIEW
         "uniform mat4x4 model_view;\n"
      #endif

      "flat out uvec4  facedata;\n"
      "     out  vec3  voxelspace_pos;\n"
      "     out  vec3  vnormal;\n"
      "     out float  texlerp;\n"
      "     out float  amb_occ;\n"

      "void main()\n"
      "{\n"
      #ifdef STBVOX_ICONFIG_FACE_ATTRIBUTE
         "   facedata = attr_face;\n"
      #else
         "   int faceID = gl_VertexID >> 2;\n"
         "   facedata   = texelFetch(facearray, faceID);\n"
      #endif

      "   vec3 offset;\n"
      "   offset.x = float( (attr_vertex       ) & 127u );\n"             
      "   offset.y = float( (attr_vertex >>  7u) & 127u );\n"             
      "   offset.z = float( (attr_vertex >> 14u) & 511u );\n"             
      "   amb_occ  = float( (attr_vertex >> 23u) &  63u ) / 63.0;\n"      
      "   texlerp  = float( (attr_vertex >> 29u)        ) /  7.0;\n"      

      "   vnormal = normal_table[(facedata.w>>2u) & 31u];\n"
      "   voxelspace_pos = offset * transform[0];\n"  
      "   vec3 position  = voxelspace_pos + transform[1];\n"  

      #ifdef STBVOX_DEBUG_TEST_NORMALS
         "   if ((facedata.w & 28u) == 16u || (facedata.w & 28u) == 24u)\n"
         "      position += vnormal.xyz * camera_pos.w;\n"
      #endif

      #ifndef STBVOX_CONFIG_OPENGL_MODELVIEW
         "   gl_Position = model_view * vec4(position,1.0);\n"
      #else
         "   gl_Position = gl_ModelViewProjectionMatrix * vec4(position,1.0);\n"
      #endif

      "}\n"
};

static const char *stbvox_fragment_program =
{
      STBVOX_SHADER_VERSION

      #if defined(STBVOX_ICONFIG_GLSL)
         "#define rlerp(t,x,y) mix(x,y,t)\n"
      #elif defined(STBVOX_CONFIG_HLSL)
         "#define rlerp(t,x,y) lerp(x,y,t)\n"
      #else
         #error "need definition of rlerp()"
      #endif

      "flat in uvec4  facedata;\n"
      "     in  vec3  voxelspace_pos;\n"
      "     in  vec3  vnormal;\n"
      "     in float  texlerp;\n"
      "     in float  amb_occ;\n"

      "uniform vec3 transform[3];\n"

      "uniform vec4 camera_pos;\n"  

      "uniform vec4 ambient[4];\n"

      #ifndef STBVOX_ICONFIG_UNTEXTURED

         "uniform sampler2DArray tex_array[2];\n"

         #ifdef STBVOX_CONFIG_PREFER_TEXBUFFER
            "uniform samplerBuffer color_table;\n"
            "uniform samplerBuffer texscale;\n"
            "uniform samplerBuffer texgen;\n"
         #else
            "uniform vec4 color_table[64];\n"
            "uniform vec4 texscale[64];\n" 
            "uniform vec3 texgen[64];\n"
         #endif
      #endif

      "out vec4  outcolor;\n"

      #if defined(STBVOX_CONFIG_LIGHTING) || defined(STBVOX_CONFIG_LIGHTING_SIMPLE)
      "vec3 compute_lighting(vec3 pos, vec3 norm, vec3 albedo, vec3 ambient);\n"
      #endif
      #if defined(STBVOX_CONFIG_FOG) || defined(STBVOX_CONFIG_FOG_SMOOTHSTEP)
      "vec3 compute_fog(vec3 color, vec3 relative_pos, float fragment_alpha);\n"
      #endif

      "void main()\n"
      "{\n"
      "   vec3 albedo;\n"
      "   float fragment_alpha;\n"

      #ifndef STBVOX_ICONFIG_UNTEXTURED

         "   uint tex1_id = facedata.x;\n"
         "   uint tex2_id = facedata.y;\n"
         "   uint texprojid = facedata.w & 31u;\n"
         "   uint color_id  = facedata.z;\n"

         #ifndef STBVOX_CONFIG_PREFER_TEXBUFFER

            "   vec3 texgen_s = texgen[texprojid];\n"
            "   vec3 texgen_t = texgen[texprojid+32u];\n"
            "   float tex1_scale = texscale[tex1_id & 63u].x;\n"
            "   vec4 color = color_table[color_id & 63u];\n"
            #ifndef STBVOX_CONFIG_DISABLE_TEX2
            "   vec4 tex2_props = texscale[tex2_id & 63u];\n"
            #endif
         #else
            "   vec3 texgen_s = texelFetch(texgen, int(texprojid)).xyz;\n"
            "   vec3 texgen_t = texelFetch(texgen, int(texprojid+32u)).xyz;\n"
            "   float tex1_scale = texelFetch(texscale, int(tex1_id & 127u)).x;\n"
            "   vec4 color = texelFetch(color_table, int(color_id & 63u));\n"
            #ifndef STBVOX_CONFIG_DISABLE_TEX2
            "   vec4 tex2_props = texelFetch(texscale, int(tex1_id & 127u));\n"
            #endif
         #endif

         #ifndef STBVOX_CONFIG_DISABLE_TEX2
         "   float tex2_scale = tex2_props.y;\n"
         "   bool texblend_mode = tex2_props.z != 0.0;\n"
         #endif
         "   vec2 texcoord;\n"
         "   vec3 texturespace_pos = voxelspace_pos + transform[2].xyz;\n"
         "   texcoord.s = dot(texturespace_pos, texgen_s);\n"
         "   texcoord.t = dot(texturespace_pos, texgen_t);\n"

         "   vec2  texcoord_1 = tex1_scale * texcoord;\n"
         #ifndef STBVOX_CONFIG_DISABLE_TEX2
         "   vec2  texcoord_2 = tex2_scale * texcoord;\n"
         #endif

         #ifdef STBVOX_CONFIG_TEX1_EDGE_CLAMP
         "   texcoord_1 = texcoord_1 - floor(texcoord_1);\n"
         "   vec4 tex1 = textureGrad(tex_array[0], vec3(texcoord_1, float(tex1_id)), dFdx(tex1_scale*texcoord), dFdy(tex1_scale*texcoord));\n"
         #else
         "   vec4 tex1 = texture(tex_array[0], vec3(texcoord_1, float(tex1_id)));\n"
         #endif

         #ifndef STBVOX_CONFIG_DISABLE_TEX2
         #ifdef STBVOX_CONFIG_TEX2_EDGE_CLAMP
         "   texcoord_2 = texcoord_2 - floor(texcoord_2);\n"
         "   vec4 tex2 = textureGrad(tex_array[0], vec3(texcoord_2, float(tex2_id)), dFdx(tex2_scale*texcoord), dFdy(tex2_scale*texcoord));\n"
         #else
         "   vec4 tex2 = texture(tex_array[1], vec3(texcoord_2, float(tex2_id)));\n"
         #endif
         #endif

         "   bool emissive = (color.a > 1.0);\n"
         "   color.a = min(color.a, 1.0);\n"

         "   if ((color_id &  64u) != 0u) tex1.rgba *= color.rgba;\n"
         "   fragment_alpha = tex1.a;\n"
         #ifndef STBVOX_CONFIG_DISABLE_TEX2
            "   if ((color_id & 128u) != 0u) tex2.rgba *= color.rgba;\n"

            #ifdef STBVOX_CONFIG_PREMULTIPLIED_ALPHA
            "   tex2.rgba *= texlerp;\n"
            #else
            "   tex2.a *= texlerp;\n"
            #endif

            "   if (texblend_mode)\n"
            "      albedo = tex1.xyz * rlerp(tex2.a, vec3(1.0,1.0,1.0), 2.0*tex2.xyz);\n"
            "   else {\n"
            #ifdef STBVOX_CONFIG_PREMULTIPLIED_ALPHA
            "      albedo = (1.0-tex2.a)*tex1.xyz + tex2.xyz;\n"
            #else
            "      albedo = rlerp(tex2.a, tex1.xyz, tex2.xyz);\n"
            #endif
            "      fragment_alpha = tex1.a*(1-tex2.a)+tex2.a;\n"
            "   }\n"
         #else
            "      albedo = tex1.xyz;\n"
         #endif

      #else 
         "   vec4 color;"
         "   color.xyz = vec3(facedata.xyz) / 255.0;\n"
         "   bool emissive = false;\n"
         "   albedo = color.xyz;\n"
         "   fragment_alpha = 1.0;\n"
      #endif

      #ifdef STBVOX_ICONFIG_VARYING_VERTEX_NORMALS

         "   vec3 normal = normalize(vnormal);\n"
      #else
         "   vec3 normal = vnormal;\n"
      #endif

      "   vec3 ambient_color = dot(normal, ambient[0].xyz) * ambient[1].xyz + ambient[2].xyz;\n"

      "   ambient_color = clamp(ambient_color, 0.0, 1.0);"
      "   ambient_color *= amb_occ;\n"

      "   vec3 lit_color;\n"
      "   if (!emissive)\n"
      #if defined(STBVOX_ICONFIG_LIGHTING) || defined(STBVOX_CONFIG_LIGHTING_SIMPLE)
         "      lit_color = compute_lighting(voxelspace_pos + transform[1], normal, albedo, ambient_color);\n"
      #else
         "      lit_color = albedo * ambient_color ;\n"
      #endif
      "   else\n"
      "      lit_color = albedo;\n"

      #if defined(STBVOX_ICONFIG_FOG) || defined(STBVOX_CONFIG_FOG_SMOOTHSTEP)
         "   vec3 dist = voxelspace_pos + (transform[1] - camera_pos.xyz);\n"
         "   lit_color = compute_fog(lit_color, dist, fragment_alpha);\n"
      #endif

      #ifdef STBVOX_CONFIG_UNPREMULTIPLY
      "   vec4 final_color = vec4(lit_color/fragment_alpha, fragment_alpha);\n"
      #else
      "   vec4 final_color = vec4(lit_color, fragment_alpha);\n"
      #endif
      "   outcolor = final_color;\n"
      "}\n"

   #ifdef STBVOX_CONFIG_LIGHTING_SIMPLE
      "\n"
      "uniform vec3 light_source[2];\n"
      "vec3 compute_lighting(vec3 pos, vec3 norm, vec3 albedo, vec3 ambient)\n"
      "{\n"
      "   vec3 light_dir = light_source[0] - pos;\n"
      "   float lambert = dot(light_dir, norm) / dot(light_dir, light_dir);\n"
      "   vec3 diffuse = clamp(light_source[1] * clamp(lambert, 0.0, 1.0), 0.0, 1.0);\n"
      "   return (diffuse + ambient) * albedo;\n"
      "}\n"
   #endif

   #ifdef STBVOX_CONFIG_FOG_SMOOTHSTEP
      "\n"
      "vec3 compute_fog(vec3 color, vec3 relative_pos, float fragment_alpha)\n"
      "{\n"
      "   float f = dot(relative_pos,relative_pos)*ambient[3].w;\n"

      "   f = clamp(f, 0.0, 1.0);\n"
      "   f = 3.0*f*f - 2.0*f*f*f;\n" 

      #ifdef STBVOX_CONFIG_PREMULTIPLIED_ALPHA
      "   return rlerp(f, color.xyz, ambient[3].xyz*fragment_alpha);\n"
      #else
      "   return rlerp(f, color.xyz, ambient[3].xyz);\n"
      #endif
      "}\n"
   #endif
};

static const char *stbvox_fragment_program_alpha_only =
{
   STBVOX_SHADER_VERSION

   "flat in uvec4  facedata;\n"
   "     in  vec3  voxelspace_pos;\n"
   "     in float  texlerp;\n"

   "uniform vec3 transform[3];\n"

   #ifndef STBVOX_ICONFIG_UNTEXTURED

      "uniform sampler2DArray tex_array[2];\n"

      #ifdef STBVOX_CONFIG_PREFER_TEXBUFFER
         "uniform samplerBuffer texscale;\n"
         "uniform samplerBuffer texgen;\n"
      #else
         "uniform vec4 texscale[64];\n" 
         "uniform vec3 texgen[64];\n"
      #endif
   #endif

   "out vec4  outcolor;\n"

   "void main()\n"
   "{\n"
   "   vec3 albedo;\n"
   "   float fragment_alpha;\n"

   #ifndef STBVOX_ICONFIG_UNTEXTURED

      "   uint tex1_id = facedata.x;\n"
      "   uint tex2_id = facedata.y;\n"
      "   uint texprojid = facedata.w & 31u;\n"
      "   uint color_id  = facedata.z;\n"

      #ifndef STBVOX_CONFIG_PREFER_TEXBUFFER

         "   vec3 texgen_s = texgen[texprojid];\n"
         "   vec3 texgen_t = texgen[texprojid+32u];\n"
         "   float tex1_scale = texscale[tex1_id & 63u].x;\n"
         "   vec4 color = color_table[color_id & 63u];\n"
         "   vec4 tex2_props = texscale[tex2_id & 63u];\n"
      #else
         "   vec3 texgen_s = texelFetch(texgen, int(texprojid)).xyz;\n"
         "   vec3 texgen_t = texelFetch(texgen, int(texprojid+32u)).xyz;\n"
         "   float tex1_scale = texelFetch(texscale, int(tex1_id & 127u)).x;\n"
         "   vec4 color = texelFetch(color_table, int(color_id & 63u));\n"
         "   vec4 tex2_props = texelFetch(texscale, int(tex2_id & 127u));\n"
      #endif

      #ifndef STBVOX_CONFIG_DISABLE_TEX2
      "   float tex2_scale = tex2_props.y;\n"
      "   bool texblend_mode = tex2_props.z &((facedata.w & 128u) != 0u);\n"
      #endif

      "   color.a = min(color.a, 1.0);\n"

      "   vec2 texcoord;\n"
      "   vec3 texturespace_pos = voxelspace_pos + transform[2].xyz;\n"
      "   texcoord.s = dot(texturespace_pos, texgen_s);\n"
      "   texcoord.t = dot(texturespace_pos, texgen_t);\n"

      "   vec2  texcoord_1 = tex1_scale * texcoord;\n"
      "   vec2  texcoord_2 = tex2_scale * texcoord;\n"

      #ifdef STBVOX_CONFIG_TEX1_EDGE_CLAMP
      "   texcoord_1 = texcoord_1 - floor(texcoord_1);\n"
      "   vec4 tex1 = textureGrad(tex_array[0], vec3(texcoord_1, float(tex1_id)), dFdx(tex1_scale*texcoord), dFdy(tex1_scale*texcoord));\n"
      #else
      "   vec4 tex1 = texture(tex_array[0], vec3(texcoord_1, float(tex1_id)));\n"
      #endif

      "   if ((color_id &  64u) != 0u) tex1.a *= color.a;\n"
      "   fragment_alpha = tex1.a;\n"

      #ifndef STBVOX_CONFIG_DISABLE_TEX2
      "   if (!texblend_mode) {\n"
         #ifdef STBVOX_CONFIG_TEX2_EDGE_CLAMP
         "      texcoord_2 = texcoord_2 - floor(texcoord_2);\n"
         "      vec4 tex2 = textureGrad(tex_array[0], vec3(texcoord_2, float(tex2_id)), dFdx(tex2_scale*texcoord), dFdy(tex2_scale*texcoord));\n"
         #else
         "      vec4 tex2 = texture(tex_array[1], vec3(texcoord_2, float(tex2_id)));\n"
         #endif

         "      tex2.a *= texlerp;\n"
         "      if ((color_id & 128u) != 0u) tex2.rgba *= color.a;\n"
         "      fragment_alpha = tex1.a*(1-tex2.a)+tex2.a;\n"
         "}\n"
      "\n"
      #endif

   #else 
      "   fragment_alpha = 1.0;\n"
   #endif

   "   outcolor = vec4(0.0, 0.0, 0.0, fragment_alpha);\n"
   "}\n"
};

STBVXDEC char *stbvox_get_vertex_shader(void)
{
   return (char *) stbvox_vertex_program;
}

STBVXDEC char *stbvox_get_fragment_shader(void)
{
   return (char *) stbvox_fragment_program;
}

STBVXDEC char *stbvox_get_fragment_shader_alpha_only(void)
{
   return (char *) stbvox_fragment_program_alpha_only;
}

static float stbvox_dummy_transform[3][3];

#ifdef STBVOX_CONFIG_PREFER_TEXBUFFER
#define STBVOX_TEXBUF 1
#else
#define STBVOX_TEXBUF 0
#endif

static stbvox_uniform_info stbvox_uniforms[] =
{
   { STBVOX_UNIFORM_TYPE_sampler  ,  4,   1, (char*) "facearray"    , 0                           },
   { STBVOX_UNIFORM_TYPE_vec3     , 12,   3, (char*) "transform"    , stbvox_dummy_transform[0]   },
   { STBVOX_UNIFORM_TYPE_sampler  ,  4,   2, (char*) "tex_array"    , 0                           },
   { STBVOX_UNIFORM_TYPE_vec4     , 16, 128, (char*) "texscale"     , stbvox_default_texscale[0] , STBVOX_TEXBUF },
   { STBVOX_UNIFORM_TYPE_vec4     , 16,  64, (char*) "color_table"  , stbvox_default_palette[0]  , STBVOX_TEXBUF },
   { STBVOX_UNIFORM_TYPE_vec3     , 12,  32, (char*) "normal_table" , stbvox_default_normals[0]   },
   { STBVOX_UNIFORM_TYPE_vec3     , 12,  64, (char*) "texgen"       , stbvox_default_texgen[0][0], STBVOX_TEXBUF },
   { STBVOX_UNIFORM_TYPE_vec4     , 16,   4, (char*) "ambient"      , stbvox_default_ambient[0]   },
   { STBVOX_UNIFORM_TYPE_vec4     , 16,   1, (char*) "camera_pos"   , stbvox_dummy_transform[0]   },
};

STBVXDEC int stbvox_get_uniform_info(stbvox_uniform_info *info, int uniform)
{
   if (uniform < 0 || uniform >= STBVOX_UNIFORM_count)
      return 0;

   *info = stbvox_uniforms[uniform];
   return 1;
}

#define STBVOX_GET_GEO(geom_data)  ((geom_data) & 15)

typedef struct
{
   unsigned char block:2;
   unsigned char overlay:2;
   unsigned char facerot:2;
   unsigned char ecolor:2;
} stbvox_rotate;

typedef struct
{
   unsigned char x,y,z;
} stbvox_pos;

static unsigned char stbvox_rotate_face[6][4] =
{
   { 0,1,2,3 },
   { 1,2,3,0 },
   { 2,3,0,1 },
   { 3,0,1,2 },
   { 4,4,4,4 },
   { 5,5,5,5 },
};

#define STBVOX_ROTATE(x,r)   stbvox_rotate_face[x][r] 

stbvox_mesh_face stbvox_compute_mesh_face_value(stbvox_mesh_maker *mm, stbvox_rotate rot, int face, int v_off, int normal)
{
   stbvox_mesh_face face_data = { 0 };
   stbvox_block_type bt = mm->input.blocktype[v_off];
   unsigned char bt_face = STBVOX_ROTATE(face, rot.block);
   int facerot = rot.facerot;

   #ifdef STBVOX_ICONFIG_UNTEXTURED
   if (mm->input.rgb) {
      face_data.tex1  = mm->input.rgb[v_off].r;
      face_data.tex2  = mm->input.rgb[v_off].g;
      face_data.color = mm->input.rgb[v_off].b;
      face_data.face_info = (normal<<2);
      return face_data;
   }
   #else
   unsigned char color_face;

   if (mm->input.color)
      face_data.color = mm->input.color[v_off];

   if (mm->input.block_tex1)
      face_data.tex1 = mm->input.block_tex1[bt];
   else if (mm->input.block_tex1_face)
      face_data.tex1 = mm->input.block_tex1_face[bt][bt_face];
   else
      face_data.tex1 = bt;

   if (mm->input.block_tex2)
      face_data.tex2 = mm->input.block_tex2[bt];
   else if (mm->input.block_tex2_face)
      face_data.tex2 = mm->input.block_tex2_face[bt][bt_face];

   if (mm->input.block_color) {
      unsigned char mcol = mm->input.block_color[bt];
      if (mcol)
         face_data.color = mcol;
   } else if (mm->input.block_color_face) {
      unsigned char mcol = mm->input.block_color_face[bt][bt_face];
      if (mcol)
         face_data.color = mcol;
   }

   if (face <= STBVOX_FACE_south) {
      if (mm->input.side_texrot)
         facerot = mm->input.side_texrot[v_off] >> (2 * face);
      else if (mm->input.block_side_texrot)
         facerot = mm->input.block_side_texrot[v_off] >> (2 * bt_face);
   }

   if (mm->input.overlay) {
      int over_face = STBVOX_ROTATE(face, rot.overlay);
      unsigned char over = mm->input.overlay[v_off];
      if (over) {
         if (mm->input.overlay_tex1) {
            unsigned char rep1 = mm->input.overlay_tex1[over][over_face];
            if (rep1)
               face_data.tex1 = rep1;
         }
         if (mm->input.overlay_tex2) {
            unsigned char rep2 = mm->input.overlay_tex2[over][over_face];
            if (rep2)
               face_data.tex2 = rep2;
         }
         if (mm->input.overlay_color) {
            unsigned char rep3 = mm->input.overlay_color[over][over_face];
            if (rep3)
               face_data.color = rep3;
         }

         if (mm->input.overlay_side_texrot && face <= STBVOX_FACE_south)
            facerot = mm->input.overlay_side_texrot[over] >> (2*over_face);
      }
   }

   if (mm->input.tex2_for_tex1)
      face_data.tex2 = mm->input.tex2_for_tex1[face_data.tex1];
   if (mm->input.tex2)
      face_data.tex2 = mm->input.tex2[v_off];
   if (mm->input.tex2_replace) {
      if (mm->input.tex2_facemask[v_off] & (1 << face))
         face_data.tex2 = mm->input.tex2_replace[v_off];
   }

   color_face = STBVOX_ROTATE(face, rot.ecolor);
   if (mm->input.extended_color) {
      unsigned char ec = mm->input.extended_color[v_off];
      if (mm->input.ecolor_facemask[ec] & (1 << color_face))
         face_data.color = mm->input.ecolor_color[ec];
   }

   if (mm->input.color2) {
      if (mm->input.color2_facemask[v_off] & (1 << color_face))
         face_data.color = mm->input.color2[v_off];
      if (mm->input.color3 && (mm->input.color3_facemask[v_off] & (1 << color_face)))
         face_data.color = mm->input.color3[v_off];
   }
   #endif

   face_data.face_info = (normal<<2) + facerot;
   return face_data;
}

enum
{
   STBVOX_FT_none    ,
   STBVOX_FT_upper   ,
   STBVOX_FT_lower   ,
   STBVOX_FT_solid   ,
   STBVOX_FT_diag_012,
   STBVOX_FT_diag_023,
   STBVOX_FT_diag_013,
   STBVOX_FT_diag_123,
   STBVOX_FT_force   , 
   STBVOX_FT_partial , 

   STBVOX_FT_count
};

static unsigned char stbvox_face_lerp[6] = { 0,2,0,2,4,4 };
static unsigned char stbvox_vert3_lerp[5] = { 0,3,6,9,12 };
static unsigned char stbvox_vert_lerp_for_face_lerp[4] = { 0, 4, 7, 7 };
static unsigned char stbvox_face3_lerp[6] = { 0,3,6,9,12,14 };
static unsigned char stbvox_vert_lerp_for_simple[4] = { 0,2,5,7 };
static unsigned char stbvox_face3_updown[8] = { 0,2,5,7,0,2,5,7 }; 

static unsigned char stbvox_vertex_vector[6][4][3] =
{
   { { 1,0,1 }, { 1,1,1 }, { 1,1,0 }, { 1,0,0 } }, 
   { { 1,1,1 }, { 0,1,1 }, { 0,1,0 }, { 1,1,0 } }, 
   { { 0,1,1 }, { 0,0,1 }, { 0,0,0 }, { 0,1,0 } }, 
   { { 0,0,1 }, { 1,0,1 }, { 1,0,0 }, { 0,0,0 } }, 
   { { 0,1,1 }, { 1,1,1 }, { 1,0,1 }, { 0,0,1 } }, 
   { { 0,0,0 }, { 1,0,0 }, { 1,1,0 }, { 0,1,0 } }, 
};

static unsigned char stbvox_vertex_selector[6][4] =
{
   { 5,7,3,1 },
   { 7,6,2,3 },
   { 6,4,0,2 },
   { 4,5,1,0 },
   { 6,7,5,4 },
   { 0,1,3,2 },
};

static stbvox_mesh_vertex stbvox_vmesh_delta_normal[6][4] =
{
   {  stbvox_vertex_encode(1,0,1,0,0) ,
      stbvox_vertex_encode(1,1,1,0,0) ,
      stbvox_vertex_encode(1,1,0,0,0) ,
      stbvox_vertex_encode(1,0,0,0,0)  },
   {  stbvox_vertex_encode(1,1,1,0,0) ,
      stbvox_vertex_encode(0,1,1,0,0) ,
      stbvox_vertex_encode(0,1,0,0,0) ,
      stbvox_vertex_encode(1,1,0,0,0)  },
   {  stbvox_vertex_encode(0,1,1,0,0) ,
      stbvox_vertex_encode(0,0,1,0,0) ,
      stbvox_vertex_encode(0,0,0,0,0) ,
      stbvox_vertex_encode(0,1,0,0,0)  },
   {  stbvox_vertex_encode(0,0,1,0,0) ,
      stbvox_vertex_encode(1,0,1,0,0) ,
      stbvox_vertex_encode(1,0,0,0,0) ,
      stbvox_vertex_encode(0,0,0,0,0)  },
   {  stbvox_vertex_encode(0,1,1,0,0) ,
      stbvox_vertex_encode(1,1,1,0,0) ,
      stbvox_vertex_encode(1,0,1,0,0) ,
      stbvox_vertex_encode(0,0,1,0,0)  },
   {  stbvox_vertex_encode(0,0,0,0,0) ,
      stbvox_vertex_encode(1,0,0,0,0) ,
      stbvox_vertex_encode(1,1,0,0,0) ,
      stbvox_vertex_encode(0,1,0,0,0)  }
};

static stbvox_mesh_vertex stbvox_vmesh_pre_vheight[6][4] =
{
   {  stbvox_vertex_encode(1,0,0,0,0) ,
      stbvox_vertex_encode(1,1,0,0,0) ,
      stbvox_vertex_encode(1,1,0,0,0) ,
      stbvox_vertex_encode(1,0,0,0,0)  },
   {  stbvox_vertex_encode(1,1,0,0,0) ,
      stbvox_vertex_encode(0,1,0,0,0) ,
      stbvox_vertex_encode(0,1,0,0,0) ,
      stbvox_vertex_encode(1,1,0,0,0)  },
   {  stbvox_vertex_encode(0,1,0,0,0) ,
      stbvox_vertex_encode(0,0,0,0,0) ,
      stbvox_vertex_encode(0,0,0,0,0) ,
      stbvox_vertex_encode(0,1,0,0,0)  },
   {  stbvox_vertex_encode(0,0,0,0,0) ,
      stbvox_vertex_encode(1,0,0,0,0) ,
      stbvox_vertex_encode(1,0,0,0,0) ,
      stbvox_vertex_encode(0,0,0,0,0)  },
   {  stbvox_vertex_encode(0,1,0,0,0) ,
      stbvox_vertex_encode(1,1,0,0,0) ,
      stbvox_vertex_encode(1,0,0,0,0) ,
      stbvox_vertex_encode(0,0,0,0,0)  },
   {  stbvox_vertex_encode(0,0,0,0,0) ,
      stbvox_vertex_encode(1,0,0,0,0) ,
      stbvox_vertex_encode(1,1,0,0,0) ,
      stbvox_vertex_encode(0,1,0,0,0)  }
};

static stbvox_mesh_vertex stbvox_vmesh_delta_half_z[6][4] =
{
   { stbvox_vertex_encode(1,0,2,0,0) ,
     stbvox_vertex_encode(1,1,2,0,0) ,
     stbvox_vertex_encode(1,1,0,0,0) ,
     stbvox_vertex_encode(1,0,0,0,0)  },
   { stbvox_vertex_encode(1,1,2,0,0) ,
     stbvox_vertex_encode(0,1,2,0,0) ,
     stbvox_vertex_encode(0,1,0,0,0) ,
     stbvox_vertex_encode(1,1,0,0,0)  },
   { stbvox_vertex_encode(0,1,2,0,0) ,
     stbvox_vertex_encode(0,0,2,0,0) ,
     stbvox_vertex_encode(0,0,0,0,0) ,
     stbvox_vertex_encode(0,1,0,0,0)  },
   { stbvox_vertex_encode(0,0,2,0,0) ,
     stbvox_vertex_encode(1,0,2,0,0) ,
     stbvox_vertex_encode(1,0,0,0,0) ,
     stbvox_vertex_encode(0,0,0,0,0)  },
   { stbvox_vertex_encode(0,1,2,0,0) ,
     stbvox_vertex_encode(1,1,2,0,0) ,
     stbvox_vertex_encode(1,0,2,0,0) ,
     stbvox_vertex_encode(0,0,2,0,0)  },
   { stbvox_vertex_encode(0,0,0,0,0) ,
     stbvox_vertex_encode(1,0,0,0,0) ,
     stbvox_vertex_encode(1,1,0,0,0) ,
     stbvox_vertex_encode(0,1,0,0,0)  }
};

static stbvox_mesh_vertex stbvox_vmesh_crossed_pair[6][4] =
{
   { stbvox_vertex_encode(1,0,2,0,0) ,
     stbvox_vertex_encode(0,1,2,0,0) ,
     stbvox_vertex_encode(0,1,0,0,0) ,
     stbvox_vertex_encode(1,0,0,0,0)  },
   { stbvox_vertex_encode(1,1,2,0,0) ,
     stbvox_vertex_encode(0,0,2,0,0) ,
     stbvox_vertex_encode(0,0,0,0,0) ,
     stbvox_vertex_encode(1,1,0,0,0)  },
   { stbvox_vertex_encode(0,1,2,0,0) ,
     stbvox_vertex_encode(1,0,2,0,0) ,
     stbvox_vertex_encode(1,0,0,0,0) ,
     stbvox_vertex_encode(0,1,0,0,0)  },
   { stbvox_vertex_encode(0,0,2,0,0) ,
     stbvox_vertex_encode(1,1,2,0,0) ,
     stbvox_vertex_encode(1,1,0,0,0) ,
     stbvox_vertex_encode(0,0,0,0,0)  },

   { stbvox_vertex_encode(0,1,2,0,0) ,
     stbvox_vertex_encode(1,1,2,0,0) ,
     stbvox_vertex_encode(1,0,2,0,0) ,
     stbvox_vertex_encode(0,0,2,0,0)  },
   { stbvox_vertex_encode(0,0,0,0,0) ,
     stbvox_vertex_encode(1,0,0,0,0) ,
     stbvox_vertex_encode(1,1,0,0,0) ,
     stbvox_vertex_encode(0,1,0,0,0)  }
};

#define STBVOX_MAX_GEOM     16
#define STBVOX_NUM_ROTATION  4

static unsigned char stbvox_hasface[STBVOX_MAX_GEOM][STBVOX_NUM_ROTATION] =
{
   { 0,0,0,0 }, 
   { 0,0,0,0 }, 
   { 63,63,63,63 }, 
   { 63,63,63,63 }, 
   { 63,63,63,63 }, 
   { 63,63,63,63 }, 
   { 1|2|4|48, 8|1|2|48, 4|8|1|48, 2|4|8|48, }, 
   { 1|2|4|48, 8|1|2|48, 4|8|1|48, 2|4|8|48, }, 
   { 47,47,47,47 }, 
   { 31,31,31,31 }, 
   { 63,63,63,63 }, 
   { 63,63,63,63 }, 
   { 63,63,63,63 }, 
   { 63,63,63,63 }, 
   { 63,63,63,63 }, 
   { 63,63,63,63 }, 
};

static unsigned char stbvox_facetype[STBVOX_GEOM_count][6] =
{
   { 0, },  
   { STBVOX_FT_solid, STBVOX_FT_solid, STBVOX_FT_solid, STBVOX_FT_solid, STBVOX_FT_solid, STBVOX_FT_solid }, 
   { STBVOX_FT_solid, STBVOX_FT_solid, STBVOX_FT_solid, STBVOX_FT_solid, STBVOX_FT_solid, STBVOX_FT_solid }, 
   { STBVOX_FT_force, STBVOX_FT_force, STBVOX_FT_force, STBVOX_FT_force, STBVOX_FT_force, STBVOX_FT_force }, 

   { STBVOX_FT_upper, STBVOX_FT_upper, STBVOX_FT_upper, STBVOX_FT_upper, STBVOX_FT_solid, STBVOX_FT_force },
   { STBVOX_FT_lower, STBVOX_FT_lower, STBVOX_FT_lower, STBVOX_FT_lower, STBVOX_FT_force, STBVOX_FT_solid },
   { STBVOX_FT_diag_123, STBVOX_FT_solid, STBVOX_FT_diag_023, STBVOX_FT_none, STBVOX_FT_force, STBVOX_FT_solid },
   { STBVOX_FT_diag_012, STBVOX_FT_solid, STBVOX_FT_diag_013, STBVOX_FT_none, STBVOX_FT_solid, STBVOX_FT_force },

   { STBVOX_FT_diag_123, STBVOX_FT_solid, STBVOX_FT_diag_023, STBVOX_FT_force, STBVOX_FT_none, STBVOX_FT_solid },
   { STBVOX_FT_diag_012, STBVOX_FT_solid, STBVOX_FT_diag_013, STBVOX_FT_force, STBVOX_FT_solid, STBVOX_FT_none },
   { STBVOX_FT_force, STBVOX_FT_force, STBVOX_FT_force, STBVOX_FT_force, 0,0 }, 
   { STBVOX_FT_force, STBVOX_FT_force, STBVOX_FT_force, STBVOX_FT_force, STBVOX_FT_force, STBVOX_FT_force }, 

   { STBVOX_FT_partial,STBVOX_FT_partial,STBVOX_FT_partial,STBVOX_FT_partial, STBVOX_FT_force, STBVOX_FT_solid }, 
   { STBVOX_FT_partial,STBVOX_FT_partial,STBVOX_FT_partial,STBVOX_FT_partial, STBVOX_FT_force, STBVOX_FT_solid }, 
   { STBVOX_FT_partial,STBVOX_FT_partial,STBVOX_FT_partial,STBVOX_FT_partial, STBVOX_FT_solid, STBVOX_FT_force }, 
   { STBVOX_FT_partial,STBVOX_FT_partial,STBVOX_FT_partial,STBVOX_FT_partial, STBVOX_FT_solid, STBVOX_FT_force }, 
};

static unsigned char stbvox_floor_slope_for_rot[4] =
{
   STBVF_su,
   STBVF_wu, 
   STBVF_nu,
   STBVF_eu,
};

static unsigned char stbvox_ceil_slope_for_rot[4] =
{
   STBVF_sd,
   STBVF_ed,
   STBVF_nd,
   STBVF_wd,
};

static unsigned short stbvox_face_visible[STBVOX_FT_count] =
{

   (unsigned short) ((~0x07ffu                                          )<<5),  
   (unsigned short) ((~((1u<<STBVOX_FT_solid) | (1<<STBVOX_FT_upper)   ))<<5),  
   (unsigned short) ((~((1u<<STBVOX_FT_solid) | (1<<STBVOX_FT_lower)   ))<<5),  
   (unsigned short) ((~((1u<<STBVOX_FT_solid)                          ))<<5),  
   (unsigned short) ((~((1u<<STBVOX_FT_solid) | (1<<STBVOX_FT_diag_013)))<<5),  
   (unsigned short) ((~((1u<<STBVOX_FT_solid) | (1<<STBVOX_FT_diag_123)))<<5),  
   (unsigned short) ((~((1u<<STBVOX_FT_solid) | (1<<STBVOX_FT_diag_012)))<<5),  
   (unsigned short) ((~((1u<<STBVOX_FT_solid) | (1<<STBVOX_FT_diag_023)))<<5),  
   (unsigned short) ((~0u                                               )<<5),  
   (unsigned short) ((~((1u<<STBVOX_FT_solid)                          ))<<5),  
};

static stbvox_mesh_vertex stbvox_geometry_vheight[8][8] =
{
   #define STBVOX_HEIGHTS(a,b,c,d,e,f,g,h) \
     { stbvox_vertex_encode(0,0,a,0,0),  \
       stbvox_vertex_encode(0,0,b,0,0),  \
       stbvox_vertex_encode(0,0,c,0,0),  \
       stbvox_vertex_encode(0,0,d,0,0),  \
       stbvox_vertex_encode(0,0,e,0,0),  \
       stbvox_vertex_encode(0,0,f,0,0),  \
       stbvox_vertex_encode(0,0,g,0,0),  \
       stbvox_vertex_encode(0,0,h,0,0) }

   STBVOX_HEIGHTS(0,0,0,0, 2,2,2,2),
   STBVOX_HEIGHTS(0,0,0,0, 2,2,2,2),
   STBVOX_HEIGHTS(0,0,0,0, 2,2,2,2),
   STBVOX_HEIGHTS(0,0,0,0, 2,2,2,2),
   STBVOX_HEIGHTS(1,1,1,1, 2,2,2,2),
   STBVOX_HEIGHTS(0,0,0,0, 1,1,1,1),
   STBVOX_HEIGHTS(0,0,0,0, 0,0,2,2),
   STBVOX_HEIGHTS(2,2,0,0, 2,2,2,2),
};

static unsigned char stbvox_rotate_vertex[8][4] =
{
   { 0,1,3,2 }, 
   { 1,3,2,0 }, 
   { 2,0,1,3 }, 
   { 3,2,0,1 }, 
   { 4,5,7,6 }, 
   { 5,7,6,4 }, 
   { 6,4,5,7 }, 
   { 7,6,4,5 }, 
};

#ifdef STBVOX_CONFIG_OPTIMIZED_VHEIGHT

static unsigned char stbvox_optimized_face_up_normal[4][4][4][4] =
{
   {
      {
         { STBVF_u   , STBVF_ne_u, STBVF_ne_u, STBVF_ne_u, },
         { STBVF_nw_u, STBVF_nu  , STBVF_nu  , STBVF_ne_u, },
         { STBVF_nw_u, STBVF_nu  , STBVF_nu  , STBVF_nu  , },
         { STBVF_nw_u, STBVF_nw_u, STBVF_nu  , STBVF_nu  , },
      },{
         { STBVF_su  , STBVF_eu  , STBVF_eu  , STBVF_ne_u, },
         { STBVF_u   , STBVF_ne_u, STBVF_ne_u, STBVF_ne_u, },
         { STBVF_nw_u, STBVF_nu  , STBVF_nu  , STBVF_ne_u, },
         { STBVF_nw_u, STBVF_nu  , STBVF_nu  , STBVF_nu  , },
      },{
         { STBVF_eu  , STBVF_eu  , STBVF_eu  , STBVF_eu  , },
         { STBVF_su  , STBVF_eu  , STBVF_eu  , STBVF_ne_u, },
         { STBVF_u   , STBVF_ne_u, STBVF_ne_u, STBVF_ne_u, },
         { STBVF_nw_u, STBVF_nu  , STBVF_nu  , STBVF_ne_u, },
      },{
         { STBVF_eu  , STBVF_eu  , STBVF_eu  , STBVF_eu  , },
         { STBVF_eu  , STBVF_eu  , STBVF_eu  , STBVF_eu  , },
         { STBVF_su  , STBVF_eu  , STBVF_eu  , STBVF_ne_u, },
         { STBVF_u   , STBVF_ne_u, STBVF_ne_u, STBVF_ne_u, },
      },
   },{
      {
         { STBVF_sw_u, STBVF_u   , STBVF_ne_u, STBVF_ne_u, },
         { STBVF_wu  , STBVF_nw_u, STBVF_nu  , STBVF_nu  , },
         { STBVF_wu  , STBVF_nw_u, STBVF_nu  , STBVF_nu  , },
         { STBVF_nw_u, STBVF_nw_u, STBVF_nw_u, STBVF_nu  , },
      },{
         { STBVF_su  , STBVF_su  , STBVF_eu  , STBVF_eu  , },
         { STBVF_sw_u, STBVF_u   , STBVF_ne_u, STBVF_ne_u, },
         { STBVF_wu  , STBVF_nw_u, STBVF_nu  , STBVF_nu  , },
         { STBVF_wu  , STBVF_nw_u, STBVF_nu  , STBVF_nu  , },
      },{
         { STBVF_su  , STBVF_eu  , STBVF_eu  , STBVF_eu  , },
         { STBVF_su  , STBVF_su  , STBVF_eu  , STBVF_eu  , },
         { STBVF_sw_u, STBVF_u   , STBVF_ne_u, STBVF_ne_u, },
         { STBVF_wu  , STBVF_nw_u, STBVF_nu  , STBVF_nu  , },
      },{
         { STBVF_su  , STBVF_eu  , STBVF_eu  , STBVF_eu  , },
         { STBVF_su  , STBVF_eu  , STBVF_eu  , STBVF_eu  , },
         { STBVF_su  , STBVF_su  , STBVF_eu  , STBVF_eu  , },
         { STBVF_sw_u, STBVF_u   , STBVF_ne_u, STBVF_ne_u, },
      },
   },{
      {
         { STBVF_sw_u, STBVF_sw_u, STBVF_u   , STBVF_ne_u, },
         { STBVF_wu  , STBVF_wu  , STBVF_nw_u, STBVF_nu  , },
         { STBVF_wu  , STBVF_wu  , STBVF_nw_u, STBVF_nu  , },
         { STBVF_wu  , STBVF_nw_u, STBVF_nw_u, STBVF_nw_u, },
      },{
         { STBVF_su  , STBVF_su  , STBVF_su  , STBVF_eu  , },
         { STBVF_sw_u, STBVF_sw_u, STBVF_u   , STBVF_ne_u, },
         { STBVF_wu  , STBVF_wu  , STBVF_nw_u, STBVF_nu  , },
         { STBVF_wu  , STBVF_wu  , STBVF_nw_u, STBVF_nu  , },
      },{
         { STBVF_su  , STBVF_su  , STBVF_eu  , STBVF_eu  , },
         { STBVF_su  , STBVF_su  , STBVF_su  , STBVF_eu  , },
         { STBVF_sw_u, STBVF_sw_u, STBVF_u   , STBVF_ne_u, },
         { STBVF_wu  , STBVF_wu  , STBVF_nw_u, STBVF_nu  , },
      },{
         { STBVF_su  , STBVF_su  , STBVF_eu  , STBVF_eu  , },
         { STBVF_su  , STBVF_su  , STBVF_eu  , STBVF_eu  , },
         { STBVF_su  , STBVF_su  , STBVF_su  , STBVF_eu  , },
         { STBVF_sw_u, STBVF_sw_u, STBVF_u   , STBVF_ne_u, },
      },
   },{
      {
         { STBVF_sw_u, STBVF_sw_u, STBVF_sw_u, STBVF_u   , },
         { STBVF_sw_u, STBVF_wu  , STBVF_wu  , STBVF_nw_u, },
         { STBVF_wu  , STBVF_wu  , STBVF_wu  , STBVF_nw_u, },
         { STBVF_wu  , STBVF_wu  , STBVF_nw_u, STBVF_nw_u, },
      },{
         { STBVF_sw_u, STBVF_su  , STBVF_su  , STBVF_su  , },
         { STBVF_sw_u, STBVF_sw_u, STBVF_sw_u, STBVF_u   , },
         { STBVF_sw_u, STBVF_wu  , STBVF_wu  , STBVF_nw_u, },
         { STBVF_wu  , STBVF_wu  , STBVF_wu  , STBVF_nw_u, },
      },{
         { STBVF_su  , STBVF_su  , STBVF_su  , STBVF_eu  , },
         { STBVF_sw_u, STBVF_su  , STBVF_su  , STBVF_su  , },
         { STBVF_sw_u, STBVF_sw_u, STBVF_sw_u, STBVF_u   , },
         { STBVF_sw_u, STBVF_wu  , STBVF_wu  , STBVF_nw_u, },
      },{
         { STBVF_su  , STBVF_su  , STBVF_su  , STBVF_eu  , },
         { STBVF_su  , STBVF_su  , STBVF_su  , STBVF_eu  , },
         { STBVF_sw_u, STBVF_su  , STBVF_su  , STBVF_su  , },
         { STBVF_sw_u, STBVF_sw_u, STBVF_sw_u, STBVF_u   , },
      },
   },
};
#else

static unsigned char stbvox_planar_face_up_normal[4][4][4] =
{
   {                                                      
      { STBVF_u   , 0         , 0         , 0          }, 
      { STBVF_u   , STBVF_u   , 0         , 0          }, 
      { STBVF_wu  , STBVF_nw_u, STBVF_nu  , 0          }, 
      { STBVF_wu  , STBVF_nw_u, STBVF_nw_u, STBVF_nu   }, 
   },{
      { STBVF_u   , STBVF_u   , 0         , 0          }, 
      { STBVF_sw_u, STBVF_u   , STBVF_ne_u, 0          }, 
      { STBVF_sw_u, STBVF_u   , STBVF_u   , STBVF_ne_u }, 
      { 0         , STBVF_wu  , STBVF_nw_u, STBVF_nu   }, 
   },{
      { STBVF_su  , STBVF_se_u, STBVF_eu  , 0          }, 
      { STBVF_sw_u, STBVF_u   , STBVF_u   , STBVF_ne_u }, 
      { 0         , STBVF_sw_u, STBVF_u   , STBVF_ne_u }, 
      { 0         , 0         , STBVF_u   , STBVF_u    }, 
   },{
      { STBVF_su  , STBVF_se_u, STBVF_se_u, STBVF_eu   }, 
      { 0         , STBVF_su  , STBVF_se_u, STBVF_eu   }, 
      { 0         , 0         , STBVF_u   , STBVF_u    }, 
      { 0         , 0         , 0         , STBVF_u    }, 
   }
};

static unsigned char stbvox_face_up_normal_012[4][4][4] =
{
   {
      { STBVF_u   , STBVF_ne_u, STBVF_ne_u, STBVF_ne_u, },
      { STBVF_wu  , STBVF_nu  , STBVF_ne_u, STBVF_ne_u, },
      { STBVF_wu  , STBVF_nw_u, STBVF_nu  , STBVF_ne_u, },
      { STBVF_wu  , STBVF_nw_u, STBVF_nw_u, STBVF_nu  , },
   },{
      { STBVF_su  , STBVF_eu  , STBVF_ne_u, STBVF_ne_u, },
      { STBVF_sw_u, STBVF_u   , STBVF_ne_u, STBVF_ne_u, },
      { STBVF_sw_u, STBVF_wu  , STBVF_nu  , STBVF_ne_u, },
      { STBVF_sw_u, STBVF_wu  , STBVF_nw_u, STBVF_nu  , },
   },{
      { STBVF_su  , STBVF_eu  , STBVF_eu  , STBVF_ne_u, },
      { STBVF_sw_u, STBVF_su  , STBVF_eu  , STBVF_ne_u, },
      { STBVF_sw_u, STBVF_sw_u, STBVF_u   , STBVF_ne_u, },
      { STBVF_sw_u, STBVF_sw_u, STBVF_wu  , STBVF_nu  , },
   },{
      { STBVF_su  , STBVF_su  , STBVF_eu  , STBVF_eu  , },
      { STBVF_sw_u, STBVF_su  , STBVF_eu  , STBVF_eu  , },
      { STBVF_sw_u, STBVF_sw_u, STBVF_su  , STBVF_eu  , },
      { STBVF_sw_u, STBVF_sw_u, STBVF_sw_u, STBVF_u   , },
   }
};

static unsigned char stbvox_face_up_normal_013[4][4][4] =
{
   {
      { STBVF_u   , STBVF_eu  , STBVF_eu  , STBVF_eu  , },
      { STBVF_nw_u, STBVF_nu  , STBVF_ne_u, STBVF_ne_u, },
      { STBVF_nw_u, STBVF_nw_u, STBVF_nu  , STBVF_ne_u, },
      { STBVF_nw_u, STBVF_nw_u, STBVF_nw_u, STBVF_nu  , },
   },{
      { STBVF_su  , STBVF_eu  , STBVF_eu  , STBVF_eu  , },
      { STBVF_wu  , STBVF_u   , STBVF_eu  , STBVF_eu  , },
      { STBVF_nw_u, STBVF_nw_u, STBVF_nu  , STBVF_ne_u, },
      { STBVF_nw_u, STBVF_nw_u, STBVF_nw_u, STBVF_nu  , },
   },{
      { STBVF_su  , STBVF_su  , STBVF_su  , STBVF_eu  , },
      { STBVF_sw_u, STBVF_su  , STBVF_eu  , STBVF_eu  , },
      { STBVF_wu  , STBVF_wu  , STBVF_u   , STBVF_eu  , },
      { STBVF_nw_u, STBVF_nw_u, STBVF_nw_u, STBVF_nu  , },
   },{
      { STBVF_su  , STBVF_su  , STBVF_su  , STBVF_eu  , },
      { STBVF_sw_u, STBVF_su  , STBVF_su  , STBVF_su  , },
      { STBVF_sw_u, STBVF_sw_u, STBVF_su  , STBVF_eu  , },
      { STBVF_wu  , STBVF_wu  , STBVF_wu  , STBVF_u   , },
   }
};

static unsigned char stbvox_face_up_normal_023[4][4][4] =
{
   {
      { STBVF_u   , STBVF_nu  , STBVF_nu  , STBVF_nu  , },
      { STBVF_eu  , STBVF_eu  , STBVF_ne_u, STBVF_ne_u, },
      { STBVF_su  , STBVF_eu  , STBVF_eu  , STBVF_ne_u, },
      { STBVF_eu  , STBVF_eu  , STBVF_eu  , STBVF_eu  , },
   },{
      { STBVF_wu  , STBVF_nw_u, STBVF_nw_u, STBVF_nw_u, },
      { STBVF_su  , STBVF_u   , STBVF_nu  , STBVF_nu  , },
      { STBVF_su  , STBVF_eu  , STBVF_eu  , STBVF_ne_u, },
      { STBVF_su  , STBVF_su  , STBVF_eu  , STBVF_eu  , },
   },{
      { STBVF_wu  , STBVF_nw_u, STBVF_nw_u, STBVF_nw_u, },
      { STBVF_sw_u, STBVF_wu  , STBVF_nw_u, STBVF_nw_u, },
      { STBVF_su  , STBVF_su  , STBVF_u   , STBVF_nu  , },
      { STBVF_su  , STBVF_su  , STBVF_eu  , STBVF_eu  , },
   },{
      { STBVF_wu  , STBVF_nw_u, STBVF_nw_u, STBVF_nw_u, },
      { STBVF_sw_u, STBVF_wu  , STBVF_nw_u, STBVF_nw_u, },
      { STBVF_sw_u, STBVF_sw_u, STBVF_wu  , STBVF_nw_u, },
      { STBVF_su  , STBVF_su  , STBVF_su  , STBVF_u   , },
   }
};

static unsigned char stbvox_face_up_normal_123[4][4][4] =
{
   {
      { STBVF_u   , STBVF_nu  , STBVF_nu  , STBVF_nu  , },
      { STBVF_eu  , STBVF_ne_u, STBVF_ne_u, STBVF_ne_u, },
      { STBVF_eu  , STBVF_ne_u, STBVF_ne_u, STBVF_ne_u, },
      { STBVF_eu  , STBVF_ne_u, STBVF_ne_u, STBVF_ne_u, },
   },{
      { STBVF_sw_u, STBVF_wu  , STBVF_nw_u, STBVF_nw_u, },
      { STBVF_su  , STBVF_u   , STBVF_nu  , STBVF_nu  , },
      { STBVF_eu  , STBVF_eu  , STBVF_ne_u, STBVF_ne_u, },
      { STBVF_eu  , STBVF_eu  , STBVF_ne_u, STBVF_ne_u, },
   },{
      { STBVF_sw_u, STBVF_sw_u, STBVF_wu  , STBVF_nw_u, },
      { STBVF_sw_u, STBVF_sw_u, STBVF_wu  , STBVF_nw_u, },
      { STBVF_su  , STBVF_su  , STBVF_u   , STBVF_nu  , },
      { STBVF_su  , STBVF_eu  , STBVF_eu  , STBVF_ne_u, },
   },{
      { STBVF_sw_u, STBVF_sw_u, STBVF_sw_u, STBVF_wu  , },
      { STBVF_sw_u, STBVF_sw_u, STBVF_sw_u, STBVF_wu  , },
      { STBVF_sw_u, STBVF_sw_u, STBVF_sw_u, STBVF_wu  , },
      { STBVF_su  , STBVF_su  , STBVF_su  , STBVF_u   , },
   }
};
#endif

void stbvox_get_quad_vertex_pointer(stbvox_mesh_maker *mm, int mesh, stbvox_mesh_vertex **vertices, stbvox_mesh_face face)
{
   char *p = mm->output_cur[mesh][0];
   int step = mm->output_step[mesh][0];

   vertices[0] = (stbvox_mesh_vertex *) p; p += step;
   vertices[1] = (stbvox_mesh_vertex *) p; p += step;
   vertices[2] = (stbvox_mesh_vertex *) p; p += step;
   vertices[3] = (stbvox_mesh_vertex *) p; p += step;
   mm->output_cur[mesh][0] = p;

   #ifdef STBVOX_ICONFIG_FACE_ATTRIBUTE

      *(stbvox_mesh_face *) (vertices[0]+1) = face;
      *(stbvox_mesh_face *) (vertices[1]+1) = face;
      *(stbvox_mesh_face *) (vertices[2]+1) = face;
      *(stbvox_mesh_face *) (vertices[3]+1) = face;
   #else
      *(stbvox_mesh_face *) mm->output_cur[mesh][1] = face;
      mm->output_cur[mesh][1] += 4;
   #endif
}

void stbvox_make_mesh_for_face(stbvox_mesh_maker *mm, stbvox_rotate rot, int face, int v_off, stbvox_pos pos, stbvox_mesh_vertex vertbase, stbvox_mesh_vertex *face_coord, unsigned char mesh, int normal)
{
   stbvox_mesh_face face_data = stbvox_compute_mesh_face_value(mm,rot,face,v_off, normal);

   stbvox_mesh_vertex p1[4] = { 0 };

   #if defined(STBVOX_CONFIG_DOWN_TEXLERP_PACKED) && defined(STBVOX_CONFIG_UP_TEXLERP_PACKED)
      #define STBVOX_USE_PACKED(f) ((f) == STBVOX_FACE_up || (f) == STBVOX_FACE_down)
   #elif defined(STBVOX_CONFIG_UP_TEXLERP_PACKED)
      #define STBVOX_USE_PACKED(f) ((f) == STBVOX_FACE_up                           )
   #elif defined(STBVOX_CONFIG_DOWN_TEXLERP_PACKED)
      #define STBVOX_USE_PACKED(f) (                         (f) == STBVOX_FACE_down)
   #endif

   #if defined(STBVOX_CONFIG_DOWN_TEXLERP_PACKED) || defined(STBVOX_CONFIG_UP_TEXLERP_PACKED)
   if (STBVOX_USE_PACKED(face)) {
      if (!mm->input.packed_compact || 0==(mm->input.packed_compact[v_off]&16))
         goto set_default;
      p1[0] = (mm->input.packed_compact[v_off + mm->cube_vertex_offset[face][0]] >> 5);
      p1[1] = (mm->input.packed_compact[v_off + mm->cube_vertex_offset[face][1]] >> 5);
      p1[2] = (mm->input.packed_compact[v_off + mm->cube_vertex_offset[face][2]] >> 5);
      p1[3] = (mm->input.packed_compact[v_off + mm->cube_vertex_offset[face][3]] >> 5);
      p1[0] = stbvox_vertex_encode(0,0,0,0,p1[0]);
      p1[1] = stbvox_vertex_encode(0,0,0,0,p1[1]);
      p1[2] = stbvox_vertex_encode(0,0,0,0,p1[2]);
      p1[3] = stbvox_vertex_encode(0,0,0,0,p1[3]);
      goto skip;
   }
   #endif

   if (mm->input.block_texlerp) {
      stbvox_block_type bt = mm->input.blocktype[v_off];
      unsigned char val = mm->input.block_texlerp[bt];
      p1[0] = p1[1] = p1[2] = p1[3] = stbvox_vertex_encode(0,0,0,0,val);
   } else if (mm->input.block_texlerp_face) {
      stbvox_block_type bt = mm->input.blocktype[v_off];
      unsigned char bt_face = STBVOX_ROTATE(face, rot.block);
      unsigned char val = mm->input.block_texlerp_face[bt][bt_face];
      p1[0] = p1[1] = p1[2] = p1[3] = stbvox_vertex_encode(0,0,0,0,val);
   } else if (mm->input.texlerp_face3) {
      unsigned char val = (mm->input.texlerp_face3[v_off] >> stbvox_face3_lerp[face]) & 7;
      if (face >= STBVOX_FACE_up)
         val = stbvox_face3_updown[val];
      p1[0] = p1[1] = p1[2] = p1[3] = stbvox_vertex_encode(0,0,0,0,val);
   } else if (mm->input.texlerp_simple) {
      unsigned char val = mm->input.texlerp_simple[v_off];
      unsigned char lerp_face = (val >> 2) & 7;
      if (lerp_face == face) {
         p1[0] = (mm->input.texlerp_simple[v_off + mm->cube_vertex_offset[face][0]] >> 5) & 7;
         p1[1] = (mm->input.texlerp_simple[v_off + mm->cube_vertex_offset[face][1]] >> 5) & 7;
         p1[2] = (mm->input.texlerp_simple[v_off + mm->cube_vertex_offset[face][2]] >> 5) & 7;
         p1[3] = (mm->input.texlerp_simple[v_off + mm->cube_vertex_offset[face][3]] >> 5) & 7;
         p1[0] = stbvox_vertex_encode(0,0,0,0,p1[0]);
         p1[1] = stbvox_vertex_encode(0,0,0,0,p1[1]);
         p1[2] = stbvox_vertex_encode(0,0,0,0,p1[2]);
         p1[3] = stbvox_vertex_encode(0,0,0,0,p1[3]);
      } else {
         unsigned char base = stbvox_vert_lerp_for_simple[val&3];
         p1[0] = p1[1] = p1[2] = p1[3] = stbvox_vertex_encode(0,0,0,0,base);
      }
   } else if (mm->input.texlerp) {
      unsigned char facelerp = (mm->input.texlerp[v_off] >> stbvox_face_lerp[face]) & 3;
      if (facelerp == STBVOX_TEXLERP_FACE_use_vert) {
         if (mm->input.texlerp_vert3 && face != STBVOX_FACE_down) {
            unsigned char shift = stbvox_vert3_lerp[face];
            p1[0] = (mm->input.texlerp_vert3[mm->cube_vertex_offset[face][0]] >> shift) & 7;
            p1[1] = (mm->input.texlerp_vert3[mm->cube_vertex_offset[face][1]] >> shift) & 7;
            p1[2] = (mm->input.texlerp_vert3[mm->cube_vertex_offset[face][2]] >> shift) & 7;
            p1[3] = (mm->input.texlerp_vert3[mm->cube_vertex_offset[face][3]] >> shift) & 7;
         } else {
            p1[0] = stbvox_vert_lerp_for_simple[mm->input.texlerp[mm->cube_vertex_offset[face][0]]>>6];
            p1[1] = stbvox_vert_lerp_for_simple[mm->input.texlerp[mm->cube_vertex_offset[face][1]]>>6];
            p1[2] = stbvox_vert_lerp_for_simple[mm->input.texlerp[mm->cube_vertex_offset[face][2]]>>6];
            p1[3] = stbvox_vert_lerp_for_simple[mm->input.texlerp[mm->cube_vertex_offset[face][3]]>>6];
         }
         p1[0] = stbvox_vertex_encode(0,0,0,0,p1[0]);
         p1[1] = stbvox_vertex_encode(0,0,0,0,p1[1]);
         p1[2] = stbvox_vertex_encode(0,0,0,0,p1[2]);
         p1[3] = stbvox_vertex_encode(0,0,0,0,p1[3]);
      } else {
         p1[0] = p1[1] = p1[2] = p1[3] = stbvox_vertex_encode(0,0,0,0,stbvox_vert_lerp_for_face_lerp[facelerp]);
      }
   } else {
      #if defined(STBVOX_CONFIG_UP_TEXLERP_PACKED) || defined(STBVOX_CONFIG_DOWN_TEXLERP_PACKED)
      set_default:
      #endif
      p1[0] = p1[1] = p1[2] = p1[3] = stbvox_vertex_encode(0,0,0,0,7); 
   }

   #if defined(STBVOX_CONFIG_UP_TEXLERP_PACKED) || defined(STBVOX_CONFIG_DOWN_TEXLERP_PACKED)
   skip:
   #endif

   {
      stbvox_mesh_vertex *mv[4];
      stbvox_get_quad_vertex_pointer(mm, mesh, mv, face_data);

      if (mm->input.lighting) {

         if (mm->input.lighting_at_vertices) {
            int i;
            for (i=0; i < 4; ++i) {
               *mv[i] = vertbase + face_coord[i]
                          + stbvox_vertex_encode(0,0,0,mm->input.lighting[v_off + mm->cube_vertex_offset[face][i]] & 63,0)
                          + p1[i];
            }
         } else {
            unsigned char *amb = &mm->input.lighting[v_off];
            int i,j;
            #if defined(STBVOX_CONFIG_ROTATION_IN_LIGHTING) || defined(STBVOX_CONFIG_VHEIGHT_IN_LIGHTING)
            #define STBVOX_GET_LIGHTING(light) ((light) & ~3)
            #define STBVOX_LIGHTING_ROUNDOFF   8
            #else
            #define STBVOX_GET_LIGHTING(light) (light)
            #define STBVOX_LIGHTING_ROUNDOFF   2
            #endif

            for (i=0; i < 4; ++i) {

               unsigned char *vamb = &amb[mm->cube_vertex_offset[face][i]];
               int total=0;
               for (j=0; j < 4; ++j)
                  total += STBVOX_GET_LIGHTING(vamb[mm->vertex_gather_offset[face][j]]);
               *mv[i] = vertbase + face_coord[i]
                          + stbvox_vertex_encode(0,0,0,(total+STBVOX_LIGHTING_ROUNDOFF)>>4,0)
                          + p1[i];

            }

         }
      } else {
         vertbase += stbvox_vertex_encode(0,0,0,63,0);
         *mv[0] = vertbase + face_coord[0] + p1[0];
         *mv[1] = vertbase + face_coord[1] + p1[1];
         *mv[2] = vertbase + face_coord[2] + p1[2];
         *mv[3] = vertbase + face_coord[3] + p1[3];
      }
   }
}

static unsigned char stbvox_reverse_face[STBVF_count] =
{
   STBVF_w, STBVF_s, STBVF_e, STBVF_n, STBVF_d   , STBVF_u   , STBVF_wd, STBVF_wu,
         0,       0,       0,       0, STBVF_sw_d, STBVF_sw_u, STBVF_sd, STBVF_su,
         0,       0,       0,       0, STBVF_se_d, STBVF_se_u, STBVF_ed, STBVF_eu,
         0,       0,       0,       0, STBVF_ne_d, STBVF_ne_d, STBVF_nd, STBVF_nu
};

#ifndef STBVOX_CONFIG_OPTIMIZED_VHEIGHT

static void stbvox_make_12_split_mesh_for_face(stbvox_mesh_maker *mm, stbvox_rotate rot, int face, int v_off, stbvox_pos pos, stbvox_mesh_vertex vertbase, stbvox_mesh_vertex *face_coord, unsigned char mesh, unsigned char *ht)
{
   stbvox_mesh_vertex v[4];

   unsigned char normal1 = stbvox_face_up_normal_012[ht[2]][ht[1]][ht[0]];
   unsigned char normal2 = stbvox_face_up_normal_123[ht[3]][ht[2]][ht[1]];

   if (face == STBVOX_FACE_down) {
      normal1 = stbvox_reverse_face[normal1];
      normal2 = stbvox_reverse_face[normal2];
   }

   v[0] = face_coord[2];
   v[1] = face_coord[3];
   v[2] = face_coord[0];
   v[3] = face_coord[2];
   stbvox_make_mesh_for_face(mm, rot, face, v_off, pos, vertbase, v, mesh, normal1);
   v[1] = face_coord[0];
   v[2] = face_coord[1];
   stbvox_make_mesh_for_face(mm, rot, face, v_off, pos, vertbase, v, mesh, normal2);
}

static void stbvox_make_03_split_mesh_for_face(stbvox_mesh_maker *mm, stbvox_rotate rot, int face, int v_off, stbvox_pos pos, stbvox_mesh_vertex vertbase, stbvox_mesh_vertex *face_coord, unsigned char mesh, unsigned char *ht)
{
   stbvox_mesh_vertex v[4];

   unsigned char normal1 = stbvox_face_up_normal_013[ht[3]][ht[1]][ht[0]];
   unsigned char normal2 = stbvox_face_up_normal_023[ht[3]][ht[2]][ht[0]];

   if (face == STBVOX_FACE_down) {
      normal1 = stbvox_reverse_face[normal1];
      normal2 = stbvox_reverse_face[normal2];
   }

   v[0] = face_coord[1];
   v[1] = face_coord[2];
   v[2] = face_coord[3];
   v[3] = face_coord[1];
   stbvox_make_mesh_for_face(mm, rot, face, v_off, pos, vertbase, v, mesh, normal1);
   v[1] = face_coord[3];
   v[2] = face_coord[0];
   stbvox_make_mesh_for_face(mm, rot, face, v_off, pos, vertbase, v, mesh, normal2);  
}
#endif

#ifndef STBVOX_CONFIG_PRECISION_Z
#define STBVOX_CONFIG_PRECISION_Z 1
#endif

static void stbvox_make_mesh_for_block(stbvox_mesh_maker *mm, stbvox_pos pos, int v_off, stbvox_mesh_vertex *vmesh)
{
   int ns_off = mm->y_stride_in_bytes;
   int ew_off = mm->x_stride_in_bytes;

   unsigned char *blockptr = &mm->input.blocktype[v_off];
   stbvox_mesh_vertex basevert = stbvox_vertex_encode(pos.x, pos.y, pos.z << STBVOX_CONFIG_PRECISION_Z , 0,0);

   stbvox_rotate rot = { 0,0,0,0 };
   unsigned char simple_rot = 0;

   unsigned char mesh = mm->default_mesh;

   if (mm->input.selector)
      mesh = mm->input.selector[v_off];
   else if (mm->input.block_selector)
      mesh = mm->input.block_selector[mm->input.blocktype[v_off]];

   if (mm->output_cur[mesh][0] + mm->output_size[mesh][0]*6 > mm->output_end[mesh][0]) {
      mm->full = 1;
      return;
   }

   #ifdef STBVOX_CONFIG_ROTATION_IN_LIGHTING
   simple_rot = mm->input.lighting[v_off] & 3;
   #endif

   if (mm->input.packed_compact)
      simple_rot = mm->input.packed_compact[v_off] & 3;

   if (blockptr[ 1]==0) {
      rot.facerot = simple_rot;
      stbvox_make_mesh_for_face(mm, rot, STBVOX_FACE_up  , v_off, pos, basevert, vmesh+4*STBVOX_FACE_up, mesh, STBVOX_FACE_up);
   }
   if (blockptr[-1]==0) {
      rot.facerot = (-simple_rot) & 3;
      stbvox_make_mesh_for_face(mm, rot, STBVOX_FACE_down, v_off, pos, basevert, vmesh+4*STBVOX_FACE_down, mesh, STBVOX_FACE_down);
   }

   if (mm->input.rotate) {
      unsigned char val = mm->input.rotate[v_off];
      rot.block   = (val >> 0) & 3;
      rot.overlay = (val >> 2) & 3;

      rot.ecolor  = (val >> 6) & 3;
   } else {
      rot.block = rot.overlay = rot.ecolor = simple_rot;
   }
   rot.facerot = 0;

   if (blockptr[ ns_off]==0)
      stbvox_make_mesh_for_face(mm, rot, STBVOX_FACE_north, v_off, pos, basevert, vmesh+4*STBVOX_FACE_north, mesh, STBVOX_FACE_north);
   if (blockptr[-ns_off]==0)
      stbvox_make_mesh_for_face(mm, rot, STBVOX_FACE_south, v_off, pos, basevert, vmesh+4*STBVOX_FACE_south, mesh, STBVOX_FACE_south);
   if (blockptr[ ew_off]==0)
      stbvox_make_mesh_for_face(mm, rot, STBVOX_FACE_east , v_off, pos, basevert, vmesh+4*STBVOX_FACE_east, mesh, STBVOX_FACE_east);
   if (blockptr[-ew_off]==0)
      stbvox_make_mesh_for_face(mm, rot, STBVOX_FACE_west , v_off, pos, basevert, vmesh+4*STBVOX_FACE_west, mesh, STBVOX_FACE_west);
}

static void stbvox_make_mesh_for_block_with_geo(stbvox_mesh_maker *mm, stbvox_pos pos, int v_off)
{
   int ns_off = mm->y_stride_in_bytes;
   int ew_off = mm->x_stride_in_bytes;
   int visible_faces, visible_base;
   unsigned char mesh;

   unsigned char bt, nbt[6];
   unsigned char geo, ngeo[6];
   unsigned char rot, nrot[6];

   bt = mm->input.blocktype[v_off];
   nbt[0] = mm->input.blocktype[v_off + ew_off];
   nbt[1] = mm->input.blocktype[v_off + ns_off];
   nbt[2] = mm->input.blocktype[v_off - ew_off];
   nbt[3] = mm->input.blocktype[v_off - ns_off];
   nbt[4] = mm->input.blocktype[v_off +      1];
   nbt[5] = mm->input.blocktype[v_off -      1];
   if (mm->input.geometry) {
      int i;
      geo = mm->input.geometry[v_off];
      ngeo[0] = mm->input.geometry[v_off + ew_off];
      ngeo[1] = mm->input.geometry[v_off + ns_off];
      ngeo[2] = mm->input.geometry[v_off - ew_off];
      ngeo[3] = mm->input.geometry[v_off - ns_off];
      ngeo[4] = mm->input.geometry[v_off +      1];
      ngeo[5] = mm->input.geometry[v_off -      1];

      rot = (geo >> 4) & 3;
      geo &= 15;
      for (i=0; i < 6; ++i) {
         nrot[i] = (ngeo[i] >> 4) & 3;
         ngeo[i] &= 15;
      }
   } else {
      int i;
      assert(mm->input.block_geometry);
      geo = mm->input.block_geometry[bt];
      for (i=0; i < 6; ++i)
         ngeo[i] = mm->input.block_geometry[nbt[i]];
      if (mm->input.selector) {
         #ifndef STBVOX_CONFIG_ROTATION_IN_LIGHTING
         if (mm->input.packed_compact == NULL) {
            rot     = (mm->input.selector[v_off         ] >> 4) & 3;
            nrot[0] = (mm->input.selector[v_off + ew_off] >> 4) & 3;
            nrot[1] = (mm->input.selector[v_off + ns_off] >> 4) & 3;
            nrot[2] = (mm->input.selector[v_off - ew_off] >> 4) & 3;
            nrot[3] = (mm->input.selector[v_off - ns_off] >> 4) & 3;
            nrot[4] = (mm->input.selector[v_off +      1] >> 4) & 3;
            nrot[5] = (mm->input.selector[v_off -      1] >> 4) & 3;
         }
         #endif
      } else {
         #ifndef STBVOX_CONFIG_ROTATION_IN_LIGHTING
         if (mm->input.packed_compact == NULL) {
            rot = (geo>>4)&3;
            geo &= 15;
            for (i=0; i < 6; ++i) {
               nrot[i] = (ngeo[i]>>4)&3;
               ngeo[i] &= 15;
            }
         }
         #endif
      }
   }

   #ifndef STBVOX_CONFIG_ROTATION_IN_LIGHTING
   if (mm->input.packed_compact) {
      rot = mm->input.packed_compact[rot] & 3;
      nrot[0] = mm->input.packed_compact[v_off + ew_off] & 3;
      nrot[1] = mm->input.packed_compact[v_off + ns_off] & 3;
      nrot[2] = mm->input.packed_compact[v_off - ew_off] & 3;
      nrot[3] = mm->input.packed_compact[v_off - ns_off] & 3;
      nrot[4] = mm->input.packed_compact[v_off +      1] & 3;
      nrot[5] = mm->input.packed_compact[v_off -      1] & 3;
   }
   #else
   rot = mm->input.lighting[v_off] & 3;
   nrot[0] = (mm->input.lighting[v_off + ew_off]) & 3;
   nrot[1] = (mm->input.lighting[v_off + ns_off]) & 3;
   nrot[2] = (mm->input.lighting[v_off - ew_off]) & 3;
   nrot[3] = (mm->input.lighting[v_off - ns_off]) & 3;
   nrot[4] = (mm->input.lighting[v_off +      1]) & 3;
   nrot[5] = (mm->input.lighting[v_off -      1]) & 3;
   #endif

   if (geo == STBVOX_GEOM_transp) {

      int i;
      for (i=0; i < 6; ++i)
         if (nbt[i] != bt) {
            nbt[i] = 0;
            ngeo[i] = STBVOX_GEOM_empty;
         } else
            ngeo[i] = STBVOX_GEOM_solid;
      geo = STBVOX_GEOM_solid;
   }

   visible_base = stbvox_hasface[geo][rot];

   visible_faces = 0;

   if (visible_base & (1 << STBVOX_FACE_east)) {
      int  type = stbvox_facetype[ geo   ][(STBVOX_FACE_east+ rot   )&3];
      int ntype = stbvox_facetype[ngeo[0]][(STBVOX_FACE_west+nrot[0])&3];
      visible_faces |= ((stbvox_face_visible[type]) >> (ntype + 5 - STBVOX_FACE_east)) & (1 << STBVOX_FACE_east);
   }
   if (visible_base & (1 << STBVOX_FACE_north)) {
      int  type = stbvox_facetype[ geo   ][(STBVOX_FACE_north+ rot   )&3];
      int ntype = stbvox_facetype[ngeo[1]][(STBVOX_FACE_south+nrot[1])&3];
      visible_faces |= ((stbvox_face_visible[type]) >> (ntype + 5 - STBVOX_FACE_north)) & (1 << STBVOX_FACE_north);
   }
   if (visible_base & (1 << STBVOX_FACE_west)) {
      int  type = stbvox_facetype[ geo   ][(STBVOX_FACE_west+ rot   )&3];
      int ntype = stbvox_facetype[ngeo[2]][(STBVOX_FACE_east+nrot[2])&3];
      visible_faces |= ((stbvox_face_visible[type]) >> (ntype + 5 - STBVOX_FACE_west)) & (1 << STBVOX_FACE_west);
   }
   if (visible_base & (1 << STBVOX_FACE_south)) {
      int  type = stbvox_facetype[ geo   ][(STBVOX_FACE_south+ rot   )&3];
      int ntype = stbvox_facetype[ngeo[3]][(STBVOX_FACE_north+nrot[3])&3];
      visible_faces |= ((stbvox_face_visible[type]) >> (ntype + 5 - STBVOX_FACE_south)) & (1 << STBVOX_FACE_south);
   }
   if (visible_base & (1 << STBVOX_FACE_up)) {
      int  type = stbvox_facetype[ geo   ][STBVOX_FACE_up];
      int ntype = stbvox_facetype[ngeo[4]][STBVOX_FACE_down];
      visible_faces |= ((stbvox_face_visible[type]) >> (ntype + 5 - STBVOX_FACE_up)) & (1 << STBVOX_FACE_up);
   }
   if (visible_base & (1 << STBVOX_FACE_down)) {
      int  type = stbvox_facetype[ geo   ][STBVOX_FACE_down];
      int ntype = stbvox_facetype[ngeo[5]][STBVOX_FACE_up];
      visible_faces |= ((stbvox_face_visible[type]) >> (ntype + 5 - STBVOX_FACE_down)) & (1 << STBVOX_FACE_down);
   }

   if (geo == STBVOX_GEOM_force)
      geo = STBVOX_GEOM_solid;

   assert((geo == STBVOX_GEOM_crossed_pair) ? (visible_faces == 15) : 1);

   if (visible_faces == 0)
      return;

   mesh = mm->default_mesh;
   if (mm->input.selector)
      mesh = mm->input.selector[v_off];
   else if (mm->input.block_selector)
      mesh = mm->input.block_selector[bt];

   if (geo <= STBVOX_GEOM_ceil_slope_north_is_bottom) {

      stbvox_mesh_vertex basevert;
      stbvox_mesh_vertex vmesh[6][4];
      stbvox_rotate rotate = { 0,0,0,0 };
      unsigned char simple_rot = rot;
      int i;

      for (i=0; i < 6*4; ++i) {
         int vert = stbvox_vertex_selector[0][i];
         vert = stbvox_rotate_vertex[vert][rot];
         vmesh[0][i] = stbvox_vmesh_pre_vheight[0][i]
                     + stbvox_geometry_vheight[geo][vert];
      }

      basevert = stbvox_vertex_encode(pos.x, pos.y, pos.z << STBVOX_CONFIG_PRECISION_Z, 0,0);
      if (mm->input.selector) {
         mesh = mm->input.selector[v_off];
      } else if (mm->input.block_selector)
         mesh = mm->input.block_selector[bt];

      if (mm->output_cur[mesh][0] + mm->output_size[mesh][0]*6 > mm->output_end[mesh][0]) {
         mm->full = 1;
         return;
      }

      if (geo >= STBVOX_GEOM_floor_slope_north_is_top) {
         if (visible_faces & (1 << STBVOX_FACE_up)) {
            int normal = geo == STBVOX_GEOM_floor_slope_north_is_top ? stbvox_floor_slope_for_rot[simple_rot] : STBVOX_FACE_up;
            rotate.facerot = simple_rot;
            stbvox_make_mesh_for_face(mm, rotate, STBVOX_FACE_up  , v_off, pos, basevert, vmesh[STBVOX_FACE_up], mesh, normal);
         }
         if (visible_faces & (1 << STBVOX_FACE_down)) {
            int normal = geo == STBVOX_GEOM_ceil_slope_north_is_bottom ? stbvox_ceil_slope_for_rot[simple_rot] : STBVOX_FACE_down;
            rotate.facerot = (-rotate.facerot) & 3;
            stbvox_make_mesh_for_face(mm, rotate, STBVOX_FACE_down, v_off, pos, basevert, vmesh[STBVOX_FACE_down], mesh, normal);
         }
      } else {
         if (visible_faces & (1 << STBVOX_FACE_up)) {
            rotate.facerot = simple_rot;
            stbvox_make_mesh_for_face(mm, rotate, STBVOX_FACE_up  , v_off, pos, basevert, vmesh[STBVOX_FACE_up], mesh, STBVOX_FACE_up);
         }
         if (visible_faces & (1 << STBVOX_FACE_down)) {
            rotate.facerot = (-rotate.facerot) & 3;
            stbvox_make_mesh_for_face(mm, rotate, STBVOX_FACE_down, v_off, pos, basevert, vmesh[STBVOX_FACE_down], mesh, STBVOX_FACE_down);
         }
      }

      if (mm->input.rotate) {
         unsigned char val = mm->input.rotate[v_off];
         rotate.block   = (val >> 0) & 3;
         rotate.overlay = (val >> 2) & 3;

         rotate.ecolor  = (val >> 6) & 3;
      } else {
         rotate.block = rotate.overlay = rotate.ecolor = simple_rot;
      }

      rotate.facerot = 0;

      if (visible_faces & (1 << STBVOX_FACE_north))
         stbvox_make_mesh_for_face(mm, rotate, STBVOX_FACE_north, v_off, pos, basevert, vmesh[STBVOX_FACE_north], mesh, STBVOX_FACE_north);
      if (visible_faces & (1 << STBVOX_FACE_south))
         stbvox_make_mesh_for_face(mm, rotate, STBVOX_FACE_south, v_off, pos, basevert, vmesh[STBVOX_FACE_south], mesh, STBVOX_FACE_south);
      if (visible_faces & (1 << STBVOX_FACE_east))
         stbvox_make_mesh_for_face(mm, rotate, STBVOX_FACE_east , v_off, pos, basevert, vmesh[STBVOX_FACE_east ], mesh, STBVOX_FACE_east);
      if (visible_faces & (1 << STBVOX_FACE_west))
         stbvox_make_mesh_for_face(mm, rotate, STBVOX_FACE_west , v_off, pos, basevert, vmesh[STBVOX_FACE_west ], mesh, STBVOX_FACE_west);
   }
   if (geo >= STBVOX_GEOM_floor_vheight_03) {

      stbvox_mesh_vertex vmesh[6][4];
      stbvox_mesh_vertex cube[8];
      stbvox_mesh_vertex basevert;
      stbvox_rotate rotate = { 0,0,0,0 };
      unsigned char simple_rot = rot;
      unsigned char ht[4];
      int extreme;

      #ifdef STBVOX_CONFIG_VHEIGHT_IN_LIGHTING
      ht[0] = mm->input.lighting[v_off              ] & 3;
      ht[1] = mm->input.lighting[v_off+ew_off       ] & 3;
      ht[2] = mm->input.lighting[v_off       +ns_off] & 3;
      ht[3] = mm->input.lighting[v_off+ew_off+ns_off] & 3;
      #else
      if (mm->input.vheight) {
         unsigned char v =  mm->input.vheight[v_off];
         ht[0] = (v >> 0) & 3;
         ht[1] = (v >> 2) & 3;
         ht[2] = (v >> 4) & 3;
         ht[3] = (v >> 6) & 3;
      } else if (mm->input.block_vheight) {
         unsigned char v = mm->input.block_vheight[bt];
         unsigned char raw[4];
         int i;

         raw[0] = (v >> 0) & 3;
         raw[1] = (v >> 2) & 3;
         raw[2] = (v >> 4) & 3;
         raw[3] = (v >> 6) & 3;

         for (i=0; i < 4; ++i)
            ht[i] = raw[stbvox_rotate_vertex[i][rot]];
      } else if (mm->input.packed_compact) {
         ht[0] = (mm->input.packed_compact[v_off              ] >> 2) & 3;
         ht[1] = (mm->input.packed_compact[v_off+ew_off       ] >> 2) & 3;
         ht[2] = (mm->input.packed_compact[v_off       +ns_off] >> 2) & 3;
         ht[3] = (mm->input.packed_compact[v_off+ew_off+ns_off] >> 2) & 3;
      } else if (mm->input.geometry) {
         ht[0] = mm->input.geometry[v_off              ] >> 6;
         ht[1] = mm->input.geometry[v_off+ew_off       ] >> 6;
         ht[2] = mm->input.geometry[v_off       +ns_off] >> 6;
         ht[3] = mm->input.geometry[v_off+ew_off+ns_off] >> 6;
      } else {
         assert(0);
      }
      #endif

      extreme = (ht[0] == 3 || ht[1] == 3 || ht[2] == 3 || ht[3] == 3);

      if (geo >= STBVOX_GEOM_ceil_vheight_03) {
         cube[0] = stbvox_vertex_encode(0,0,ht[0],0,0);
         cube[1] = stbvox_vertex_encode(0,0,ht[1],0,0);
         cube[2] = stbvox_vertex_encode(0,0,ht[2],0,0);
         cube[3] = stbvox_vertex_encode(0,0,ht[3],0,0);
         cube[4] = stbvox_vertex_encode(0,0,2,0,0);
         cube[5] = stbvox_vertex_encode(0,0,2,0,0);
         cube[6] = stbvox_vertex_encode(0,0,2,0,0);
         cube[7] = stbvox_vertex_encode(0,0,2,0,0);
      } else {
         cube[0] = stbvox_vertex_encode(0,0,0,0,0);
         cube[1] = stbvox_vertex_encode(0,0,0,0,0);
         cube[2] = stbvox_vertex_encode(0,0,0,0,0);
         cube[3] = stbvox_vertex_encode(0,0,0,0,0);
         cube[4] = stbvox_vertex_encode(0,0,ht[0],0,0);
         cube[5] = stbvox_vertex_encode(0,0,ht[1],0,0);
         cube[6] = stbvox_vertex_encode(0,0,ht[2],0,0);
         cube[7] = stbvox_vertex_encode(0,0,ht[3],0,0);
      }
      if (!mm->input.vheight && mm->input.block_vheight) {

      }

      {
         int i;
         for (i=0; i < 6*4; ++i) {
            int vert = stbvox_vertex_selector[0][i];
            vmesh[0][i] = stbvox_vmesh_pre_vheight[0][i]
                        + cube[vert];
         }
      }

      basevert = stbvox_vertex_encode(pos.x, pos.y, pos.z << STBVOX_CONFIG_PRECISION_Z, 0,0);

      if (mm->output_cur[mesh][0] + mm->output_size[mesh][0]*6 > mm->output_end[mesh][0]) {
         mm->full = 1;
         return;
      }

      if (visible_faces & (1 << STBVOX_FACE_up)) {
         if (geo >= STBVOX_GEOM_ceil_vheight_03)

            stbvox_make_mesh_for_face(mm, rotate, STBVOX_FACE_up  , v_off, pos, basevert, vmesh[STBVOX_FACE_up], mesh, STBVOX_FACE_up);
         else {
         #ifndef STBVOX_CONFIG_OPTIMIZED_VHEIGHT

            if (cube[5] + cube[6] != cube[4] + cube[7]) {

               if (geo == STBVOX_GEOM_floor_vheight_03)
                  stbvox_make_03_split_mesh_for_face(mm, rotate, STBVOX_FACE_up, v_off, pos, basevert, vmesh[STBVOX_FACE_up], mesh, ht);
               else
                  stbvox_make_12_split_mesh_for_face(mm, rotate, STBVOX_FACE_up, v_off, pos, basevert, vmesh[STBVOX_FACE_up], mesh, ht);
            } else
               stbvox_make_mesh_for_face(mm, rotate, STBVOX_FACE_up  , v_off, pos, basevert, vmesh[STBVOX_FACE_up], mesh, stbvox_planar_face_up_normal[ht[2]][ht[1]][ht[0]]);
         #else
            stbvox_make_mesh_for_face(mm, rotate, STBVOX_FACE_up  , v_off, pos, basevert, vmesh[STBVOX_FACE_up], mesh, stbvox_optimized_face_up_normal[ht[3]][ht[2]][ht[1]][ht[0]]);
         #endif
         }
      }
      if (visible_faces & (1 << STBVOX_FACE_down)) {
         if (geo < STBVOX_GEOM_ceil_vheight_03)

            stbvox_make_mesh_for_face(mm, rotate, STBVOX_FACE_down, v_off, pos, basevert, vmesh[STBVOX_FACE_down], mesh, STBVOX_FACE_down);
         else {
         #ifndef STBVOX_CONFIG_OPTIMIZED_VHEIGHT

            if (cube[1] + cube[2] != cube[0] + cube[3]) {

               if (geo == STBVOX_GEOM_ceil_vheight_03)
                  stbvox_make_03_split_mesh_for_face(mm, rotate, STBVOX_FACE_down, v_off, pos, basevert, vmesh[STBVOX_FACE_down], mesh, ht);
               else
                  stbvox_make_12_split_mesh_for_face(mm, rotate, STBVOX_FACE_down, v_off, pos, basevert, vmesh[STBVOX_FACE_down], mesh, ht);
            } else
               stbvox_make_mesh_for_face(mm, rotate, STBVOX_FACE_down, v_off, pos, basevert, vmesh[STBVOX_FACE_down], mesh, stbvox_reverse_face[stbvox_planar_face_up_normal[ht[2]][ht[1]][ht[0]]]);
         #else
            stbvox_make_mesh_for_face(mm, rotate, STBVOX_FACE_down, v_off, pos, basevert, vmesh[STBVOX_FACE_down], mesh, stbvox_reverse_face[stbvox_optimized_face_up_normal[ht[3]][ht[2]][ht[1]][ht[0]]]);
         #endif
         }
      }

      if (mm->input.rotate) {
         unsigned char val = mm->input.rotate[v_off];
         rotate.block   = (val >> 0) & 3;
         rotate.overlay = (val >> 2) & 3;

         rotate.ecolor  = (val >> 6) & 3;
      } else if (mm->input.selector) {
         rotate.block = rotate.overlay = rotate.ecolor = simple_rot;
      }

      if ((visible_faces & (1 << STBVOX_FACE_north)) || (extreme && (ht[2] == 3 || ht[3] == 3)))
         stbvox_make_mesh_for_face(mm, rotate, STBVOX_FACE_north, v_off, pos, basevert, vmesh[STBVOX_FACE_north], mesh, STBVOX_FACE_north);
      if ((visible_faces & (1 << STBVOX_FACE_south)) || (extreme && (ht[0] == 3 || ht[1] == 3)))
         stbvox_make_mesh_for_face(mm, rotate, STBVOX_FACE_south, v_off, pos, basevert, vmesh[STBVOX_FACE_south], mesh, STBVOX_FACE_south);
      if ((visible_faces & (1 << STBVOX_FACE_east)) || (extreme && (ht[1] == 3 || ht[3] == 3)))
         stbvox_make_mesh_for_face(mm, rotate, STBVOX_FACE_east , v_off, pos, basevert, vmesh[STBVOX_FACE_east ], mesh, STBVOX_FACE_east);
      if ((visible_faces & (1 << STBVOX_FACE_west)) || (extreme && (ht[0] == 3 || ht[2] == 3)))
         stbvox_make_mesh_for_face(mm, rotate, STBVOX_FACE_west , v_off, pos, basevert, vmesh[STBVOX_FACE_west ], mesh, STBVOX_FACE_west);
   }

   if (geo == STBVOX_GEOM_crossed_pair) {

      stbvox_mesh_vertex basevert = stbvox_vertex_encode(pos.x, pos.y, pos.z << STBVOX_CONFIG_PRECISION_Z , 0,0);
      unsigned char simple_rot=0;
      stbvox_rotate rot = { 0,0,0,0 };
      unsigned char mesh = mm->default_mesh;
      if (mm->input.selector) {
         mesh = mm->input.selector[v_off];
         simple_rot = mesh >> 4;
         mesh &= 15;
      }
      if (mm->input.block_selector) {
         mesh = mm->input.block_selector[bt];
      }

      if (mm->output_cur[mesh][0] + mm->output_size[mesh][0]*4 > mm->output_end[mesh][0]) {
         mm->full = 1;
         return;
      }

      if (mm->input.rotate) {
         unsigned char val = mm->input.rotate[v_off];
         rot.block   = (val >> 0) & 3;
         rot.overlay = (val >> 2) & 3;

         rot.ecolor  = (val >> 6) & 3;
      } else if (mm->input.selector) {
         rot.block = rot.overlay = rot.ecolor = simple_rot;
      }
      rot.facerot = 0;

      stbvox_make_mesh_for_face(mm, rot, STBVOX_FACE_north, v_off, pos, basevert, stbvox_vmesh_crossed_pair[STBVOX_FACE_north], mesh, STBVF_ne_u_cross);
      stbvox_make_mesh_for_face(mm, rot, STBVOX_FACE_south, v_off, pos, basevert, stbvox_vmesh_crossed_pair[STBVOX_FACE_south], mesh, STBVF_sw_u_cross);
      stbvox_make_mesh_for_face(mm, rot, STBVOX_FACE_east , v_off, pos, basevert, stbvox_vmesh_crossed_pair[STBVOX_FACE_east ], mesh, STBVF_se_u_cross);
      stbvox_make_mesh_for_face(mm, rot, STBVOX_FACE_west , v_off, pos, basevert, stbvox_vmesh_crossed_pair[STBVOX_FACE_west ], mesh, STBVF_nw_u_cross);
   }

}

static void stbvox_make_mesh_for_column(stbvox_mesh_maker *mm, int x, int y, int z0)
{
   stbvox_pos pos;
   int v_off = x * mm->x_stride_in_bytes + y * mm->y_stride_in_bytes;
   int ns_off = mm->y_stride_in_bytes;
   int ew_off = mm->x_stride_in_bytes;
   pos.x = x;
   pos.y = y;
   pos.z = 0;
   if (mm->input.geometry) {
      unsigned char *bt  = mm->input.blocktype + v_off;
      unsigned char *geo = mm->input.geometry + v_off;
      int z;
      for (z=z0; z < mm->z1; ++z) {
         if (bt[z] && ( !bt[z+ns_off] || !STBVOX_GET_GEO(geo[z+ns_off]) || !bt[z-ns_off] || !STBVOX_GET_GEO(geo[z-ns_off])
                      || !bt[z+ew_off] || !STBVOX_GET_GEO(geo[z+ew_off]) || !bt[z-ew_off] || !STBVOX_GET_GEO(geo[z-ew_off])
                      || !bt[z-1] || !STBVOX_GET_GEO(geo[z-1]) || !bt[z+1] || !STBVOX_GET_GEO(geo[z+1])))
         {  
            pos.z = z;
            stbvox_make_mesh_for_block_with_geo(mm, pos, v_off+z);
            if (mm->full) {
               mm->cur_z = z;
               return;
            }
         }
      }
   } else if (mm->input.block_geometry) {
      int z;
      unsigned char *bt  = mm->input.blocktype + v_off;
      unsigned char *geo = mm->input.block_geometry;
      for (z=z0; z < mm->z1; ++z) {
         if (bt[z] && (    geo[bt[z+ns_off]] != STBVOX_GEOM_solid
                        || geo[bt[z-ns_off]] != STBVOX_GEOM_solid
                        || geo[bt[z+ew_off]] != STBVOX_GEOM_solid
                        || geo[bt[z-ew_off]] != STBVOX_GEOM_solid
                        || geo[bt[z-1]] != STBVOX_GEOM_solid
                        || geo[bt[z+1]] != STBVOX_GEOM_solid))
         {
            pos.z = z;
            stbvox_make_mesh_for_block_with_geo(mm, pos, v_off+z);
            if (mm->full) {
               mm->cur_z = z;
               return;
            }
         }
      }
   } else {
      unsigned char *bt = mm->input.blocktype + v_off;
      int z;
      #if STBVOX_CONFIG_PRECISION_Z == 1
      stbvox_mesh_vertex *vmesh = stbvox_vmesh_delta_half_z[0];
      #else
      stbvox_mesh_vertex *vmesh = stbvox_vmesh_delta_normal[0];
      #endif
      for (z=z0; z < mm->z1; ++z) {

         if (bt[z] && (!bt[z+ns_off] || !bt[z-ns_off] || !bt[z+ew_off] || !bt[z-ew_off] || !bt[z-1] || !bt[z+1])) {
            pos.z = z;
            stbvox_make_mesh_for_block(mm, pos, v_off+z, vmesh);
            if (mm->full) {
               mm->cur_z = z;
               return;
            }
         }
      }
   }
}

static void stbvox_bring_up_to_date(stbvox_mesh_maker *mm)
{
   if (mm->config_dirty) {
      int i;
      #ifdef STBVOX_ICONFIG_FACE_ATTRIBUTE
         mm->num_mesh_slots = 1;
         for (i=0; i < STBVOX_MAX_MESHES; ++i) {
            mm->output_size[i][0] = 32;
            mm->output_step[i][0] = 8;
         }
      #else
         mm->num_mesh_slots = 2;
         for (i=0; i < STBVOX_MAX_MESHES; ++i) {
            mm->output_size[i][0] = 16;
            mm->output_step[i][0] = 4;
            mm->output_size[i][1] = 4;
            mm->output_step[i][1] = 4;
         }
      #endif

      mm->config_dirty = 0;
   }
}

int stbvox_make_mesh(stbvox_mesh_maker *mm)
{
   int x,y;
   stbvox_bring_up_to_date(mm);
   mm->full = 0;
   if (mm->cur_x > mm->x0 || mm->cur_y > mm->y0 || mm->cur_z > mm->z0) {
      stbvox_make_mesh_for_column(mm, mm->cur_x, mm->cur_y, mm->cur_z);
      if (mm->full)
         return 0;
      ++mm->cur_y;
      while (mm->cur_y < mm->y1 && !mm->full) {
         stbvox_make_mesh_for_column(mm, mm->cur_x, mm->cur_y, mm->z0);
         if (mm->full)
            return 0;
         ++mm->cur_y;
      }
      ++mm->cur_x;
   }
   for (x=mm->cur_x; x < mm->x1; ++x) {
      for (y=mm->y0; y < mm->y1; ++y) {
         stbvox_make_mesh_for_column(mm, x, y, mm->z0);
         if (mm->full) {
            mm->cur_x = x;
            mm->cur_y = y;
            return 0;
         }
      }
   }
   return 1;
}

void stbvox_init_mesh_maker(stbvox_mesh_maker *mm)
{
   memset(mm, 0, sizeof(*mm));
   stbvox_build_default_palette();

   mm->config_dirty = 1;
   mm->default_mesh = 0;
}

int stbvox_get_buffer_count(stbvox_mesh_maker *mm)
{
   stbvox_bring_up_to_date(mm);
   return mm->num_mesh_slots;
}

int stbvox_get_buffer_size_per_quad(stbvox_mesh_maker *mm, int n)
{
   return mm->output_size[0][n];
}

void stbvox_reset_buffers(stbvox_mesh_maker *mm)
{
   int i;
   for (i=0; i < STBVOX_MAX_MESHES*STBVOX_MAX_MESH_SLOTS; ++i) {
      mm->output_cur[0][i] = 0;
      mm->output_buffer[0][i] = 0;
   }
}

void stbvox_set_buffer(stbvox_mesh_maker *mm, int mesh, int slot, void *buffer, size_t len)
{
   int i;
   stbvox_bring_up_to_date(mm);
   mm->output_buffer[mesh][slot] = (char *) buffer;
   mm->output_cur   [mesh][slot] = (char *) buffer;
   mm->output_len   [mesh][slot] = (int) len;
   mm->output_end   [mesh][slot] = (char *) buffer + len;
   for (i=0; i < STBVOX_MAX_MESH_SLOTS; ++i) {
      if (mm->output_buffer[mesh][i]) {
         assert(mm->output_len[mesh][i] / mm->output_size[mesh][i] == mm->output_len[mesh][slot] / mm->output_size[mesh][slot]);
      }
   }
}

void stbvox_set_default_mesh(stbvox_mesh_maker *mm, int mesh)
{
   mm->default_mesh = mesh;
}

int stbvox_get_quad_count(stbvox_mesh_maker *mm, int mesh)
{
   return (int) ((mm->output_cur[mesh][0] - mm->output_buffer[mesh][0]) / mm->output_size[mesh][0]);
}

stbvox_input_description *stbvox_get_input_description(stbvox_mesh_maker *mm)
{
   return &mm->input;
}

void stbvox_set_input_range(stbvox_mesh_maker *mm, int x0, int y0, int z0, int x1, int y1, int z1)
{
   mm->x0 = x0;
   mm->y0 = y0;
   mm->z0 = z0;

   mm->x1 = x1;
   mm->y1 = y1;
   mm->z1 = z1;

   mm->cur_x = x0;
   mm->cur_y = y0;
   mm->cur_z = z0;

}

void stbvox_get_transform(stbvox_mesh_maker *mm, float transform[3][3])
{

   transform[0][0] = 1.0;
   transform[0][1] = 1.0;
   #if STBVOX_CONFIG_PRECISION_Z==1
   transform[0][2] = 0.5f;
   #else
   transform[0][2] = 1.0f;
   #endif

   transform[1][0] = (float) (mm->pos_x);
   transform[1][1] = (float) (mm->pos_y);
   transform[1][2] = (float) (mm->pos_z);

   transform[2][0] = (float) (mm->pos_x & 255); 
   transform[2][1] = (float) (mm->pos_y & 255);
   transform[2][2] = (float) (mm->pos_z & 255);
}

void stbvox_get_bounds(stbvox_mesh_maker *mm, float bounds[2][3])
{
   bounds[0][0] = (float) (mm->pos_x + mm->x0);
   bounds[0][1] = (float) (mm->pos_y + mm->y0);
   bounds[0][2] = (float) (mm->pos_z + mm->z0);
   bounds[1][0] = (float) (mm->pos_x + mm->x1);
   bounds[1][1] = (float) (mm->pos_y + mm->y1);
   bounds[1][2] = (float) (mm->pos_z + mm->z1);
}

void stbvox_set_mesh_coordinates(stbvox_mesh_maker *mm, int x, int y, int z)
{
   mm->pos_x = x;
   mm->pos_y = y;
   mm->pos_z = z;
}

void stbvox_set_input_stride(stbvox_mesh_maker *mm, int x_stride_in_bytes, int y_stride_in_bytes)
{
   int f,v;
   mm->x_stride_in_bytes = x_stride_in_bytes;
   mm->y_stride_in_bytes = y_stride_in_bytes;
   for (f=0; f < 6; ++f) {
      for (v=0; v < 4; ++v) {
         mm->cube_vertex_offset[f][v]   =   stbvox_vertex_vector[f][v][0]    * mm->x_stride_in_bytes
                                         +  stbvox_vertex_vector[f][v][1]    * mm->y_stride_in_bytes
                                         +  stbvox_vertex_vector[f][v][2]                           ;
         mm->vertex_gather_offset[f][v] =  (stbvox_vertex_vector[f][v][0]-1) * mm->x_stride_in_bytes
                                         + (stbvox_vertex_vector[f][v][1]-1) * mm->y_stride_in_bytes
                                         + (stbvox_vertex_vector[f][v][2]-1)                        ;
      }
   }
}

#if 0

static char *normal_names[32] =
{
   0,0,0,0,"u   ",0, "eu  ",0,
   0,0,0,0,"ne_u",0, "nu  ",0,
   0,0,0,0,"nw_u",0, "wu  ",0,
   0,0,0,0,"sw_u",0, "su  ",0,
};

static char *find_best_normal(float x, float y, float z)
{
   int best_slot = 4;
   float best_dot = 0;
   int i;
   for (i=0; i < 32; ++i) {
      if (normal_names[i]) {
         float dot = x * stbvox_default_normals[i][0] + y * stbvox_default_normals[i][1] + z * stbvox_default_normals[i][2];
         if (dot > best_dot) {
            best_dot = dot;
            best_slot = i;
         }
      }
   }
   return normal_names[best_slot];
}

int main(int argc, char **argv)
{
   int sw,se,nw,ne;
   for (ne=0; ne < 4; ++ne) {
      for (nw=0; nw < 4; ++nw) {
         for (se=0; se < 4; ++se) {
            printf("        { ");
            for (sw=0; sw < 4; ++sw) {
               float x = (float) (nw + sw - ne - se);
               float y = (float) (sw + se - nw - ne);
               float z = 2;
               printf("STBVF_%s, ", find_best_normal(x,y,z));
            }
            printf("},\n");
         }
      }
   }
   return 0;
}
#endif

#endif 
