#include "terrain.h"
#include <math.h>
#include <stddef.h>
/** Smooth bounded mask between two thresholds. */
static double smooth(double a,double b,double x)
{
    double t=fmax(0,fmin(1,(x-a)/(b-a)));
    return t*t*(3-2*t);
}
/** Evaluates a scaled rotated octave field on a spherical domain. */
static double fractal(const AxNoise *n,const double p[3],double scale,unsigned octaves,int ridged)
{
    double q[3]={p[0]*scale,p[1]*scale,p[2]*scale};
    return ax_noise_fractal3(n,q,octaves,2.03,0.5,ridged);
}
/** Composes depressions and compact raised rims without discontinuous cell-identity parameters. */
static double craters(const AxTerrain *t,const double p[3],double scale)
{
    double q[3]={p[0]*scale+13.7,p[1]*scale-3.1,p[2]*scale+27.9};
    double d=ax_noise_cellular3(&t->landforms,q)/0.6;
    double bowl=-0.75*pow(fmax(0,1-d*d),2);
    double rim=0.2*exp(-pow((d-0.95)*9,2))*(1-smooth(1.1,1.35,d));
    return bowl+rim;
}
bool ax_terrain_init(AxTerrain *t,AxTerrainKind kind,uint64_t seed,double radius,double relief,double frequency,double cave_depth)
{
    if (!t || kind<AX_TERRAIN_WAVES || kind>=AX_TERRAIN_COUNT || !isfinite(radius) || radius<1 || radius>1e12 ||
        !isfinite(relief) || relief<0 || relief>radius*0.25 || !isfinite(frequency) || frequency<0 || frequency>16 ||
        !isfinite(cave_depth) || cave_depth<0 || cave_depth>fmin(1e6,radius*0.1)) return false;
    *t=(AxTerrain){.kind=kind,.radius=radius,.relief=relief,.frequency=frequency,.cave_depth=cave_depth};
    ax_noise_init(&t->broad,seed); ax_noise_init(&t->landforms,seed^UINT64_C(0x391e51d3)); ax_noise_init(&t->detail,seed^UINT64_C(0x914a83b1));
    for (size_t k=0;k<3;++k) t->phase[k]=(double)(ax_noise_hash64(seed+k)>>11)*0x1p-53*6.283185307179586;
    return true;
}
/** Composes radial height and optional climate with the caller-provided radial distance. */
static double height_at(const AxTerrain *t,const double p[3],double r,AxTerrainClimate *climate)
{
    double q[3]={0},height=0;
    if (r>0) for (size_t k=0;k<3;++k) q[k]=p[k]/r*t->radius;
    if (t->relief>0) {
        if (t->kind==AX_TERRAIN_WAVES) {
            height=t->relief;
            for (size_t k=0;k<3;++k) height*=sin(p[k]*t->frequency+t->phase[k]);
        } else if (t->kind==AX_TERRAIN_CONTINENTAL) {
            double domain[3]={q[0]/t->radius*2.4,q[1]/t->radius*2.4,q[2]/t->radius*2.4},warped[3];
            ax_noise_warp3(&t->broad,domain,0.65,warped);
            double continent=ax_noise_fractal3(&t->broad,warped,4,2.03,0.5,0);
            double land=smooth(-0.15,0.18,continent);
            double province=smooth(-0.1,0.3,fractal(&t->landforms,q,12/t->radius,3,0))*land;
            double ridge=fractal(&t->landforms,q,65/t->radius,4,1);
            double hills=fractal(&t->detail,q,350/t->radius,3,0)*0.035*land;
            double detail=fractal(&t->detail,q,t->frequency,2,0)*0.003*land;
            height=t->relief*(-0.42+0.54*land+0.74*province*ridge*ridge*ridge+hills+detail);
        } else {
            double base=fractal(&t->broad,q,8/t->radius,3,0)*0.18;
            height=t->relief*(base+craters(t,q,35/t->radius)*0.6+craters(t,q,160/t->radius)*0.2+
                fractal(&t->detail,q,t->frequency,2,0)*0.02);
        }
    }
    if (climate) {
        climate->elevation=height;
        climate->temperature=fmax(0,fmin(1,1-(r>0 ? fabs(p[1])/r : 0)-fmax(height,0)/fmax(t->relief,1)*0.5));
        climate->moisture=0.5+0.5*fractal(&t->broad,q,21/t->radius,2,0);
    }
    return height;
}
/** Rejects nonfinite/unaddressable positions before trigonometry or lattice conversion. */
static bool valid_position(const AxTerrain *t,const double p[3])
{
    if (!t || !p || !(t->radius>0)) return false;
    for (size_t k=0;k<3;++k) if (!isfinite(p[k]) || fabs(p[k])>0x1p65) return false;
    return true;
}
double ax_terrain_height(const AxTerrain *t,const double p[3],AxTerrainClimate *climate)
{
    if (!valid_position(t,p)) { if (climate) *climate=(AxTerrainClimate){0}; return 0; }
    return height_at(t,p,hypot(hypot(p[0],p[1]),p[2]),climate);
}
double ax_terrain_density(const AxTerrain *t,const double p[3],AxTerrainClimate *climate)
{
    if (!valid_position(t,p)) { if (climate) *climate=(AxTerrainClimate){0}; return INFINITY; }
    double r=hypot(hypot(p[0],p[1]),p[2]);
    double field=r-t->radius-height_at(t,p,r,climate);
    if (t->cave_depth>0 && field<-3 && field>-t->cave_depth-fmax(8,t->cave_depth*0.25)) {
        double scale=1/fmax(32,t->cave_depth);
        double a=fractal(&t->landforms,p,scale,2,0),b=fractal(&t->detail,p,scale,2,0);
        double tube=(hypot(a,b)-0.045)/scale;
        double depth_gate=fmax(field+6,-field-t->cave_depth);
        double blend=smooth(-t->cave_depth-fmax(8,t->cave_depth*0.25),-t->cave_depth,field);
        field=fmax(field,field+blend*(-fmax(tube,depth_gate)-field));
    }
    return field;
}
