#ifndef STB_INCLUDE_STB_HEXWAVE_H
#define STB_INCLUDE_STB_HEXWAVE_H

#ifndef STB_HEXWAVE_MAX_BLEP_LENGTH
#define STB_HEXWAVE_MAX_BLEP_LENGTH   64 
#endif

#ifdef STB_HEXWAVE_STATIC
#define STB_HEXWAVE_DEF static
#else
#define STB_HEXWAVE_DEF extern
#endif

typedef struct HexWave HexWave;

STB_HEXWAVE_DEF void hexwave_init(int width, int oversample, float *user_buffer);

STB_HEXWAVE_DEF void hexwave_shutdown(float *user_buffer);

STB_HEXWAVE_DEF void hexwave_create(HexWave *hex, int reflect, float peak_time, float half_height, float zero_wait);

STB_HEXWAVE_DEF void hexwave_change(HexWave *hex, int reflect, float peak_time, float half_height, float zero_wait);

STB_HEXWAVE_DEF void hexwave_generate_samples(float *output, int num_samples, HexWave *hex, float freq);

typedef struct
{
   int   reflect;
   float peak_time;
   float zero_wait;
   float half_height;
} HexWaveParameters;

struct HexWave
{
   float t, prev_dt;
   HexWaveParameters current, pending;
   int have_pending;
   float buffer[STB_HEXWAVE_MAX_BLEP_LENGTH];
}; 
#endif

#ifdef STB_HEXWAVE_IMPLEMENTATION

#ifndef STB_HEXWAVE_NO_ALLOCATION
#include <stdlib.h> 
#endif

#include <string.h> 
#include <math.h>   

#define hexwave_clamp(v,a,b)   ((v) < (a) ? (a) : (v) > (b) ? (b) : (v))

STB_HEXWAVE_DEF void hexwave_change(HexWave *hex, int reflect, float peak_time, float half_height, float zero_wait)
{
   hex->pending.reflect     = reflect;
   hex->pending.peak_time   = hexwave_clamp(peak_time,0,1);
   hex->pending.half_height = half_height;
   hex->pending.zero_wait   = hexwave_clamp(zero_wait,0,1);

   hex->have_pending        = 1;
}

STB_HEXWAVE_DEF void hexwave_create(HexWave *hex, int reflect, float peak_time, float half_height, float zero_wait)
{
   memset(hex, 0, sizeof(*hex));
   hexwave_change(hex, reflect, peak_time, half_height, zero_wait);
   hex->current = hex->pending;
   hex->have_pending = 0;
   hex->t = 0;
   hex->prev_dt = 0;
}

static struct
{
   int width;       
   int oversample;  
   float *blep;
   float *blamp;
} hexblep;

static void hex_add_oversampled_bleplike(float *output, float time_since_transition, float scale, float *data)
{
   float *d1,*d2;
   float lerpweight;
   int i, bw = hexblep.width;

   int slot = (int) (time_since_transition * hexblep.oversample);
   if (slot >= hexblep.oversample)
      slot = hexblep.oversample-1; 

   d1 = &data[ slot   *bw];
   d2 = &data[(slot+1)*bw];

   lerpweight = time_since_transition * hexblep.oversample - slot;
   for (i=0; i < bw; ++i)
      output[i] += scale * (d1[i] + (d2[i]-d1[i])*lerpweight);
}

static void hex_blep (float *output, float time_since_transition, float scale)
{
   hex_add_oversampled_bleplike(output, time_since_transition, scale, hexblep.blep);
}

static void hex_blamp(float *output, float time_since_transition, float scale)
{
   hex_add_oversampled_bleplike(output, time_since_transition, scale, hexblep.blamp);
}

typedef struct
{
   float t,v,s; 
} hexvert;

static void hexwave_generate_linesegs(hexvert vert[9], HexWave *hex, float dt)
{
   int j;
   float min_len = dt / 256.0f;

   vert[0].t = 0;
   vert[0].v = 0;
   vert[1].t = hex->current.zero_wait*0.5f;
   vert[1].v = 0;
   vert[2].t = 0.5f*hex->current.peak_time + vert[1].t*(1-hex->current.peak_time);
   vert[2].v = 1;
   vert[3].t = 0.5f;
   vert[3].v = hex->current.half_height;

   if (hex->current.reflect) {
      for (j=4; j <= 7; ++j) {
         vert[j].t = 1 -  vert[7-j].t;
         vert[j].v =    - vert[7-j].v;
      }
   } else {
      for (j=4; j <= 7; ++j) {
         vert[j].t =  0.5f +  vert[j-4].t;
         vert[j].v =        - vert[j-4].v;
      }
   }
   vert[8].t = 1;
   vert[8].v = 0;

   for (j=0; j < 8; ++j) {
      if (vert[j+1].t <= vert[j].t + min_len) {

         vert[j+1].t = vert[j].t;
      }
   }

   if (vert[8].t != 1.0f) {

      float t = vert[8].t;
      for (j=5; j <= 8; ++j)
         if (vert[j].t == t)
            vert[j].t = 1.0f;
   }

   for (j=0; j < 8; ++j)
      if (vert[j+1].t == vert[j].t)
         vert[j].s = 0;
      else
         vert[j].s = (vert[j+1].v - vert[j].v) / (vert[j+1].t - vert[j].t);

   vert[8].t = 1;
   vert[8].v = vert[0].v;
   vert[8].s = vert[0].s;
}

STB_HEXWAVE_DEF void hexwave_generate_samples(float *output, int num_samples, HexWave *hex, float freq)
{
   hexvert vert[9];
   int pass,i,j;
   float t = hex->t;
   float temp_output[2*STB_HEXWAVE_MAX_BLEP_LENGTH];
   int buffered_length = sizeof(float)*hexblep.width;
   float dt = (float) fabs(freq);
   float recip_dt = (dt == 0.0f) ? 0.0f : 1.0f / dt;

   int halfw = hexblep.width/2;

   if (num_samples <= 0)
      return;

   hexwave_generate_linesegs(vert, hex, dt);

   if (hex->prev_dt != dt) {

      float slope;
      for (j=1; j < 6; ++j)
         if (t < vert[j].t)
            break;
      slope = vert[j].s;
      if (slope != 0)
         hex_blamp(output, 0, (dt - hex->prev_dt)*slope);
      hex->prev_dt = dt;
   }

   memset(output, 0, sizeof(float)*num_samples);
   memset(temp_output, 0, 2*hexblep.width*sizeof(float));

   if (num_samples >= hexblep.width) {
      memcpy(output, hex->buffer, buffered_length);
   } else {

      memcpy(temp_output, hex->buffer, buffered_length);
   }

   for (pass=0; pass < 2; ++pass) {
      int i0,i1;
      float *out;

      if (pass == 0) {
         if (num_samples < hexblep.width)
            continue;

         out = output;
         i0 = 0;
         i1 = num_samples - hexblep.width;
      } else {

         out = temp_output;
         i0 = 0;
         if (num_samples >= hexblep.width)
            i1 = hexblep.width;
         else
            i1 = num_samples;
      }

      for (j=0; j < 8; ++j)
         if (t < vert[j+1].t)                                  
            break;

      i = i0;
      for(;;) {
         while (t < vert[j+1].t) {
            if (i == i1)
               goto done;
            out[i+halfw] += vert[j].v + vert[j].s*(t - vert[j].t);
            t += dt;
            ++i;
         }

         if (vert[j].t == vert[j+1].t)
            hex_blep(out+i, recip_dt*(t-vert[j+1].t), (vert[j+1].v - vert[j].v));
         hex_blamp(out+i, recip_dt*(t-vert[j+1].t), dt*(vert[j+1].s - vert[j].s));
         ++j;

         if (j == 8) {

            j = 0;
            t -= 1.0; 
            if (hex->have_pending) {
               float prev_s0 = vert[j].s;
               float prev_v0 = vert[j].v;
               hex->current = hex->pending;
               hex->have_pending = 0;
               hexwave_generate_linesegs(vert, hex, dt);

               if (vert[j].v != prev_v0)
                  hex_blep (out+i, recip_dt*t,    (vert[j].v - prev_v0));
               if (vert[j].s != prev_s0)
                  hex_blamp(out+i, recip_dt*t, dt*(vert[j].s - prev_s0));
            }
         }
      }
     done:
      ;
   }

   if (num_samples >= hexblep.width) {

      for (i=0; i < hexblep.width; ++i)
         output[num_samples-hexblep.width + i] += temp_output[i];
      memcpy(hex->buffer, temp_output+hexblep.width, buffered_length);
   } else {
      for (i=0; i < num_samples; ++i)
         output[i] = temp_output[i];
      memcpy(hex->buffer, temp_output+num_samples, buffered_length);
   }

   hex->t = t;
}

STB_HEXWAVE_DEF void hexwave_shutdown(float *user_buffer)
{
   #ifndef STB_HEXWAVE_NO_ALLOCATION
   if (user_buffer != 0) {
      free(hexblep.blep);
      free(hexblep.blamp);
   }
   #endif
}

STB_HEXWAVE_DEF void hexwave_init(int width, int oversample, float *user_buffer)
{
   int halfwidth = width/2;
   int half = halfwidth*oversample;
   int blep_buffer_count = width*(oversample+1);
   int n = 2*half+1;
#ifdef STB_HEXWAVE_NO_ALLOCATION
   float *buffers = user_buffer;
#else
   float *buffers = user_buffer ? user_buffer : (float *) malloc(sizeof(float) * n * 2);
#endif
   float *step    = buffers+0*n;
   float *ramp    = buffers+1*n;
   float *blep_buffer, *blamp_buffer;
   double integrate_impulse=0, integrate_step=0;
   int i,j;

   if (width > STB_HEXWAVE_MAX_BLEP_LENGTH)
      width = STB_HEXWAVE_MAX_BLEP_LENGTH;

   if (user_buffer == 0) {
      #ifndef STB_HEXWAVE_NO_ALLOCATION
      blep_buffer  = (float *) malloc(sizeof(float)*blep_buffer_count);
      blamp_buffer = (float *) malloc(sizeof(float)*blep_buffer_count);
      #endif
   } else {
      blep_buffer  = ramp+n;
      blamp_buffer = blep_buffer + blep_buffer_count;
   }

   for (i=0; i < n; ++i) {
      for (j=0; j < 16; ++j) {
         float sinc_t = 3.141592f* (i-half) / oversample;
         float sinc   = (i==half) ? 1.0f : (float) sin(sinc_t) / (sinc_t);
         float wt     = 2.0f*3.1415926f * i / (n-1);
         float window = (float) (0.355768 - 0.487396*cos(wt) + 0.144232*cos(2*wt) - 0.012604*cos(3*wt)); 
         double value       =         window * sinc;
         integrate_impulse +=         value/16;
         integrate_step    +=         integrate_impulse/16;
      }
      step[i]            = (float) integrate_impulse;
      ramp[i]            = (float) integrate_step;
   }

   for (i=0; i < n; ++i) {
      step[i] = step[i] * (float) (1.0       / step[n-1]); 
      ramp[i] = ramp[i] * (float) (halfwidth / ramp[n-1]); 
   }

   for (j=0; j <= oversample; ++j) {
      for (i=0; i < width; ++i) {
         blep_buffer [j*width+i] = step[j+i*oversample];
         blamp_buffer[j*width+i] = ramp[j+i*oversample];
      }
   }

   for (j=0; j <= oversample; ++j) {

      for (i=halfwidth; i < width; ++i)
         blep_buffer [j*width+i] -= 1.0f;

      for (i=halfwidth; i < width; ++i)
         blamp_buffer[j*width+i] -= (j+i*oversample-half)*(1.0f/oversample);
   }

   hexblep.blep  = blep_buffer;
   hexblep.blamp = blamp_buffer;
   hexblep.width = width;
   hexblep.oversample = oversample;

   #ifndef STB_HEXWAVE_NO_ALLOCATION
   if (user_buffer == 0)
      free(buffers);
   #endif
}
#endif 
