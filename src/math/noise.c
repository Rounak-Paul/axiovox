#include "noise.h"
#include <math.h>
#include <stddef.h>
/** Scrambles a seed or lattice address using defined unsigned arithmetic. */
uint64_t ax_noise_hash64(uint64_t n)
{
    n=(n^(n>>30))*UINT64_C(0xbf58476d1ce4e5b9);
    n=(n^(n>>27))*UINT64_C(0x94d049bb133111eb);
    return n^(n>>31);
}
/** Validates positions before lattice conversion and preserves useful double fractional precision. */
static int valid(const AxNoise *noise,const double p[3])
{
    if (!noise || !p) return 0;
    for (size_t k=0;k<3;++k) if (!isfinite(p[k]) || fabs(p[k])>0x1p50) return 0;
    return 1;
}
/** Quintic interpolation and its derivative vanish at lattice boundaries. */
static double fade(double t) { return t*t*t*(t*(t*6-15)+10); }
/** Returns the derivative of fade at t in [0,1]. */
static double fade_derivative(double t) { return 30*t*t*(t-1)*(t-1); }
/** Resolves a corner gradient index through the immutable permutation. */
static unsigned corner(const AxNoise *n,int64_t x,int64_t y,int64_t z)
{
    const uint8_t *p=n->permutation;
    return p[((uint64_t)x&255)+p[((uint64_t)y&255)+p[(uint64_t)z&255]]];
}
void ax_noise_init(AxNoise *noise,uint64_t seed)
{
    if (!noise) return;
    noise->seed=seed;
    for (unsigned i=0;i<256;++i) noise->permutation[i]=(uint8_t)i;
    for (unsigned i=255;i>0;--i) {
        unsigned j=(unsigned)(ax_noise_hash64(seed+i)%(i+1));
        uint8_t swap=noise->permutation[i]; noise->permutation[i]=noise->permutation[j]; noise->permutation[j]=swap;
    }
    for (unsigned i=0;i<256;++i) noise->permutation[i+256]=noise->permutation[i];
}
/** Samples corner gradients, optionally accumulating analytic derivatives for derivative consumers. */
static AxNoiseSample gradient_sample(const AxNoise *noise,const double p[3],int derivatives)
{
    AxNoiseSample result={0};
    if (!valid(noise,p)) return result;
    static const int gradients[12][3]={{1,1,0},{-1,1,0},{1,-1,0},{-1,-1,0},{1,0,1},{-1,0,1},
        {1,0,-1},{-1,0,-1},{0,1,1},{0,-1,1},{0,1,-1},{0,-1,-1}};
    int64_t cell[3]; double t[3],w[3],dw[3];
    for (size_t k=0;k<3;++k) { cell[k]=(int64_t)floor(p[k]); t[k]=p[k]-cell[k]; w[k]=fade(t[k]); dw[k]=fade_derivative(t[k]); }
    for (unsigned i=0;i<8;++i) {
        unsigned bit[3]={i&1,(i>>1)&1,(i>>2)&1};
        const int *g=gradients[corner(noise,cell[0]+bit[0],cell[1]+bit[1],cell[2]+bit[2])%12];
        double weights[3],dot=0;
        for (size_t k=0;k<3;++k) { weights[k]=bit[k] ? w[k] : 1-w[k]; dot+=g[k]*(t[k]-bit[k]); }
        double weight=weights[0]*weights[1]*weights[2];
        result.value+=weight*dot;
        if (derivatives) for (size_t k=0;k<3;++k) result.gradient[k]+=weight*g[k]+dot*(bit[k] ? dw[k] : -dw[k])*weights[(k+1)%3]*weights[(k+2)%3];
    }
    if (result.value<=-1 || result.value>=1) {
        result.value=fmax(-1,fmin(1,result.value));
        for (size_t k=0;k<3;++k) result.gradient[k]=0;
    }
    return result;
}
AxNoiseSample ax_noise_gradient3(const AxNoise *noise,const double p[3])
{
    return gradient_sample(noise,p,1);
}
double ax_noise_value3(const AxNoise *noise,const double p[3])
{
    if (!valid(noise,p)) return 0;
    int64_t cell[3]; double w[3];
    for (size_t k=0;k<3;++k) { cell[k]=(int64_t)floor(p[k]); w[k]=fade(p[k]-cell[k]); }
    double result=0;
    for (unsigned i=0;i<8;++i) {
        unsigned a=i&1,b=(i>>1)&1,c=(i>>2)&1;
        double value=corner(noise,cell[0]+a,cell[1]+b,cell[2]+c)/127.5-1;
        result+=value*(a ? w[0] : 1-w[0])*(b ? w[1] : 1-w[1])*(c ? w[2] : 1-w[2]);
    }
    return result;
}
double ax_noise_fractal3(const AxNoise *noise,const double p[3],unsigned octaves,double lacunarity,double gain,int ridged)
{
    if (!valid(noise,p) || !isfinite(lacunarity) || lacunarity<1 || lacunarity>4 || !isfinite(gain) || gain<0 || gain>1) return 0;
    if (!octaves) octaves=1;
    if (octaves>8) octaves=8;
    double q[3]={p[0],p[1],p[2]},sum=0,weight=1,total=0;
    for (unsigned i=0;i<octaves;++i) {
        double n=gradient_sample(noise,q,0).value;
        sum+=(ridged ? 1-fabs(n) : n)*weight; total+=weight; weight*=gain;
        double x=q[0],y=q[1],z=q[2];
        q[0]=(0.36*x+0.48*y-0.8*z)*lacunarity+19.1;
        q[1]=(-0.8*x+0.6*y)*lacunarity+7.7;
        q[2]=(0.48*x+0.64*y+0.6*z)*lacunarity+31.3;
    }
    return sum/total;
}
double ax_noise_cellular3(const AxNoise *noise,const double p[3])
{
    if (!valid(noise,p)) return 0;
    int64_t cell[3]; double t[3];
    for (size_t k=0;k<3;++k) { cell[k]=(int64_t)floor(p[k]); t[k]=p[k]-cell[k]; }
    double nearest=INFINITY;
    for (int z=-1;z<=1;++z) for (int y=-1;y<=1;++y) for (int x=-1;x<=1;++x) {
        uint64_t h=ax_noise_hash64(((uint64_t)(cell[0]+x)*UINT64_C(0x9e3779b185ebca87)+
            (uint64_t)(cell[1]+y)*UINT64_C(0xc2b2ae3d27d4eb4f)+(uint64_t)(cell[2]+z)*UINT64_C(0x165667b19e3779f9))^noise->seed);
        double a=x+0.3+0.4*(double)(h&65535)/65535-t[0];
        double b=y+0.3+0.4*(double)((h>>16)&65535)/65535-t[1];
        double c=z+0.3+0.4*(double)((h>>32)&65535)/65535-t[2];
        nearest=fmin(nearest,a*a+b*b+c*c);
    }
    return sqrt(nearest);
}
void ax_noise_warp3(const AxNoise *noise,const double p[3],double amplitude,double out[3])
{
    if (!out || !p) return;
    double q[3]={p[0],p[1],p[2]};
    if (!isfinite(amplitude)) amplitude=0;
    for (size_t k=0;k<3;++k) {
        double channel[3]={q[0]+k*37.7,q[1]-k*19.3,q[2]+k*53.1};
        out[k]=q[k]+amplitude*gradient_sample(noise,channel,0).value;
    }
}

double ax_noise_erosion3(const AxNoise *noise,const double p[3],unsigned octaves)
{
    if (!valid(noise,p)) return 0;
    if (!octaves) octaves=1;
    if (octaves>8) octaves=8;
    double sum=0,total=0,amplitude=1,frequency=1,drift[3]={0};
    for (unsigned i=0;i<octaves;++i) {
        double q[3];
        for (size_t k=0;k<3;++k) q[k]=(p[k]+0.18*drift[k])*frequency+i*19.19;
        AxNoiseSample sample=gradient_sample(noise,q,1);
        double n=sample.value*1.8;
        double ridge=1-fmin(1,sqrt(n*n+0.0025)-0.05);
        ridge*=ridge;
        sum+=amplitude*ridge;
        total+=amplitude;
        for (size_t k=0;k<3;++k) drift[k]-=amplitude*sample.gradient[k]*n;
        frequency*=2.03;
        amplitude*=0.48*fmax(0.1,ridge);
    }
    return sum/total;
}
