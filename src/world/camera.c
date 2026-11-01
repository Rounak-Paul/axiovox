#include "camera.h"
#include <math.h>
#include <stddef.h>
#define AX_PI 3.14159265358979323846f
#define AX_PITCH_LIMIT (AX_PI * 0.5f - 0.001f)
#define AX_MAX_FRAME_TIME 0.1
/** Clamps pitch short of the poles so the basis never degenerates. */
static float clamp_pitch(float pitch)
{
    if (!isfinite(pitch)) return 0.0f;
    return pitch > AX_PITCH_LIMIT ? AX_PITCH_LIMIT : pitch < -AX_PITCH_LIMIT ? -AX_PITCH_LIMIT : pitch;
}
/** Wraps yaw into [-pi, pi] to preserve float precision over long sessions. */
static float wrap_yaw(float yaw)
{
    if (!isfinite(yaw)) return 0.0f;
    return remainderf(yaw, 2.0f * AX_PI);
}
void ax_camera_init(AxCamera *camera, const float position[3], float yaw, float pitch,const double up[3])
{
    if (!camera) return;
    *camera = (AxCamera){.yaw = wrap_yaw(yaw), .pitch = clamp_pitch(pitch), .fov_y = 70.0f * AX_PI / 180.0f,
        .reference_up={0,1,0}, .reference_forward={0,0,-1},
        .move_speed = 6.0f, .boost = 4.0f, .sensitivity = 0.0025f};
    double length=up ? hypot(hypot(up[0],up[1]),up[2]) : 0;
    if (isfinite(length) && length>1e-12) {
        for (size_t k=0;k<3;++k) camera->reference_up[k]=(float)(up[k]/length);
        double heading[3]={0,0,-1};
        if (fabs(camera->reference_up[2])>0.9) { heading[0]=1; heading[2]=0; }
        double projection=0;
        for (size_t k=0;k<3;++k) projection+=heading[k]*camera->reference_up[k];
        for (size_t k=0;k<3;++k) heading[k]-=projection*camera->reference_up[k];
        length=hypot(hypot(heading[0],heading[1]),heading[2]);
        for (size_t k=0;k<3;++k) camera->reference_forward[k]=(float)(heading[k]/length);
    }
    if (position)
        for (int i = 0; i < 3; ++i) camera->position[i] = isfinite(position[i]) ? position[i] : 0.0f;
}
void ax_camera_basis(const AxCamera *camera, float forward[3], float right[3], float up[3])
{
    float cy=cosf(camera->yaw),sy=sinf(camera->yaw),cp=cosf(camera->pitch),sp=sinf(camera->pitch);
    const float *v=camera->reference_forward,*n=camera->reference_up;
    float base_right[3]={v[1]*n[2]-v[2]*n[1],v[2]*n[0]-v[0]*n[2],v[0]*n[1]-v[1]*n[0]};
    float f[3],r[3],u[3];
    for (size_t k=0;k<3;++k) { f[k]=(v[k]*cy+base_right[k]*sy)*cp+n[k]*sp; r[k]=base_right[k]*cy-v[k]*sy; }
    u[0]=r[1]*f[2]-r[2]*f[1]; u[1]=r[2]*f[0]-r[0]*f[2]; u[2]=r[0]*f[1]-r[1]*f[0];
    for (int i = 0; i < 3; ++i) {
        if (forward) forward[i] = f[i];
        if (right) right[i] = r[i];
        if (up) up[i] = u[i];
    }
}
void ax_camera_update(AxCamera *camera, const AxInput *input, double dt)
{
    if (!camera || !input || !isfinite(dt) || dt <= 0) return;
    if (dt > AX_MAX_FRAME_TIME) dt = AX_MAX_FRAME_TIME;
    double dx, dy;
    if (ax_input_mouse_delta(input, &dx, &dy)) {
        camera->yaw = wrap_yaw(camera->yaw + (float)dx * camera->sensitivity);
        camera->pitch = clamp_pitch(camera->pitch - (float)dy * camera->sensitivity);
    }
    float forward[3], right[3];
    ax_camera_basis(camera, forward, right, NULL);
    float axial = (float)ax_input_down(input, AX_KEY_W) - (float)ax_input_down(input, AX_KEY_S);
    float lateral = (float)ax_input_down(input, AX_KEY_D) - (float)ax_input_down(input, AX_KEY_A);
    float vertical = (float)ax_input_down(input, AX_KEY_E) - (float)ax_input_down(input, AX_KEY_Q);
    float move[3];
    for (size_t k=0;k<3;++k) move[k]=forward[k]*axial+right[k]*lateral+camera->reference_up[k]*vertical;
    float length = sqrtf(move[0] * move[0] + move[1] * move[1] + move[2] * move[2]);
    if (length < 1e-6f) return;
    bool boosted = ax_input_down(input, AX_KEY_LEFT_SHIFT) || ax_input_down(input, AX_KEY_RIGHT_SHIFT);
    float distance = camera->move_speed * (boosted ? camera->boost : 1.0f) * (float)dt / length;
    for (int i = 0; i < 3; ++i) camera->position[i] += move[i] * distance;
}

void ax_camera_align(AxCamera *camera,const double up[3],double influence,double dt)
{
    if (!camera || !up || !isfinite(influence) || influence<=0 || !isfinite(dt) || dt<=0) return;
    double length=hypot(hypot(up[0],up[1]),up[2]);
    if (!isfinite(length) || length<1e-12) return;
    double target[3],axis[3];
    for (size_t k=0;k<3;++k) target[k]=up[k]/length;
    const float *old=camera->reference_up;
    axis[0]=old[1]*target[2]-old[2]*target[1];
    axis[1]=old[2]*target[0]-old[0]*target[2];
    axis[2]=old[0]*target[1]-old[1]*target[0];
    double dot=fmax(-1,fmin(1,old[0]*target[0]+old[1]*target[1]+old[2]*target[2]));
    double axis_length=hypot(hypot(axis[0],axis[1]),axis[2]);
    if (axis_length<1e-8) {
        if (dot>0) return;
        for (size_t k=0;k<3;++k) axis[k]=camera->reference_forward[k];
        axis_length=1;
    }
    for (size_t k=0;k<3;++k) axis[k]/=axis_length;
    double angle=acos(dot)*(1-exp(-12*fmin(influence,1)*fmin(dt,0.1)));
    double cosine=cos(angle),sine=sin(angle);
    float *vectors[2]={camera->reference_up,camera->reference_forward};
    for (size_t i=0;i<2;++i) {
        double v[3]={vectors[i][0],vectors[i][1],vectors[i][2]};
        double cross[3]={axis[1]*v[2]-axis[2]*v[1],axis[2]*v[0]-axis[0]*v[2],axis[0]*v[1]-axis[1]*v[0]};
        double projection=axis[0]*v[0]+axis[1]*v[1]+axis[2]*v[2];
        for (size_t k=0;k<3;++k) vectors[i][k]=(float)(v[k]*cosine+cross[k]*sine+axis[k]*projection*(1-cosine));
    }
    length=hypot(hypot(camera->reference_up[0],camera->reference_up[1]),camera->reference_up[2]);
    for (size_t k=0;k<3;++k) camera->reference_up[k]=(float)(camera->reference_up[k]/length);
    double projection=0;
    for (size_t k=0;k<3;++k) projection+=camera->reference_forward[k]*camera->reference_up[k];
    for (size_t k=0;k<3;++k) camera->reference_forward[k]-=(float)(projection*camera->reference_up[k]);
    length=hypot(hypot(camera->reference_forward[0],camera->reference_forward[1]),camera->reference_forward[2]);
    for (size_t k=0;k<3;++k) camera->reference_forward[k]=(float)(camera->reference_forward[k]/length);
}
