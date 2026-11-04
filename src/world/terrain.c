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
/** Samples compact seeded impacts at scale (inverse meters); neighboring cells overlap continuously. */
static double craters(const AxTerrain *t,const double p[3],double scale)
{
    double q[3]={p[0]*scale+13.7,p[1]*scale-3.1,p[2]*scale+27.9},f[3];
    int64_t cell[3];
    for (size_t k=0;k<3;++k) { cell[k]=(int64_t)floor(q[k]); f[k]=q[k]-cell[k]; }
    double result=0;
    for (int z=-1;z<=1;++z) for (int y=-1;y<=1;++y) for (int x=-1;x<=1;++x) {
        uint64_t h=ax_noise_hash64(((uint64_t)(cell[0]+x)*UINT64_C(0x9e3779b185ebca87)+
            (uint64_t)(cell[1]+y)*UINT64_C(0xc2b2ae3d27d4eb4f)+(uint64_t)(cell[2]+z)*UINT64_C(0x165667b19e3779f9))^t->landforms.seed);
        if ((h>>60)<5) continue;
        double radius=0.16+0.30*(double)((h>>48)&4095)/4095;
        double dx=x+0.25+0.5*(double)(h&65535)/65535-f[0];
        double dy=y+0.25+0.5*(double)((h>>16)&65535)/65535-f[1];
        double dz=z+0.25+0.5*(double)((h>>32)&65535)/65535-f[2];
        double distance=sqrt(dx*dx+dy*dy+dz*dz)/radius;
        if (distance>=1.3) continue;
        double bowl=-0.72*(1-smooth(0.25,1,distance));
        double rim=0.22*(1-smooth(0,0.22,fabs(distance-1)));
        double peak=0.18*smooth(0.3,0.46,radius)*(1-smooth(0,0.22,distance));
        result+=(bowl+rim+peak)*(radius/0.46);
    }
    return fmax(-1,fmin(1,result));
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
/** Composes one continent's shelf, coastal relief, connected ranges and valley floors.
 * q is a planet-local surface position; climate receives landform masks when requested.
 */
static double continental_height(const AxTerrain *t,const double q[3],AxTerrainClimate *climate)
{
    double local_radius=fmax(t->radius,100000),domain[3],warped[3];
    for (size_t k=0;k<3;++k) domain[k]=q[k]/t->radius*2.4;
    ax_noise_warp3(&t->broad,domain,0.65,warped);
    double latitude=fabs(q[1])/t->radius;
    double continent=ax_noise_fractal3(&t->broad,warped,3,2.03,0.38,0)+0.045-0.16*latitude*latitude;
    double coast_band=1-smooth(0.04,0.14,fabs(continent-0.01));
    double coast=continent-0.012+coast_band*(0.025*fractal(&t->landforms,q,28/t->radius,3,0)+
        0.008*fractal(&t->detail,q,160/t->radius,2,0));
    double interior=smooth(0.015,0.14,continent);
    double shelf=-0.34+0.31*smooth(-0.20,-0.04,coast);
    double lowland=shelf+0.06*smooth(-0.04,0.04,coast)+0.035*smooth(0.04,0.18,coast);
    for (size_t k=0;k<3;++k) domain[k]=q[k]*8/local_radius;
    ax_noise_warp3(&t->landforms,domain,0.8,warped);
    double fault=ax_noise_fractal3(&t->landforms,warped,2,2.03,0.35,0);
    double belt=1-smooth(0.04,0.28,fabs(fault));
    double spine=1-smooth(0.015,0.13,fabs(fault));
    double margin=smooth(0.025,0.09,continent)*(1-smooth(0.14,0.25,continent))*smooth(-0.12,0.12,fault);
    double province=1-(1-belt*interior)*(1-margin);
    double mountain_domain[3];
    for (size_t k=0;k<3;++k) mountain_domain[k]=q[k]*65/local_radius+warped[k]*0.35;
    double branches=ax_noise_erosion3(&t->landforms,mountain_domain,4);
    double drainage=fabs(fractal(&t->broad,mountain_domain,1.7,2,0));
    double valley=1-smooth(0.035,0.15,drainage);
    double crest=spine*(0.72+0.28*branches);
    double mass=province*(0.12+0.48*crest+0.40*branches*branches);
    mass*=1-valley*(0.98-0.18*spine);
    double hills=fractal(&t->detail,q,240/local_radius,2,0);
    double piedmont=province*(1-valley)*0.035*hills;
    double upland=0.80*mass+piedmont;
    double terrace_step=fmax(1,t->relief*0.0125);
    double terrace=lowland*t->relief/terrace_step;
    double terraced=(floor(terrace)+smooth(0.25,0.75,terrace-floor(terrace)))*terrace_step;
    double terrace_weight=smooth(0.04,0.10,coast)*(1-smooth(0.15,0.25,coast))*valley*0.45;
    double cliffs=0.025*smooth(0.08,0.22,fault)*(1-valley)*
        (smooth(0.002,0.004,coast)-smooth(0.015,0.06,coast));
    double base=lowland*t->relief+(terraced-lowland*t->relief)*terrace_weight+
        t->relief*cliffs*(t->radius/local_radius);
    double detail=fractal(&t->detail,q,t->frequency,2,0)*0.002*province*(1-valley);
    if (climate) climate->valley=valley*province;
    return base+t->relief*(upland*(t->radius/local_radius)+detail);
}
/** Composes radial height and optional climate with the caller-provided radial distance. */
static double height_at(const AxTerrain *t,const double p[3],double r,AxTerrainClimate *climate)
{
    double q[3]={0},height=0;
    if (climate) *climate=(AxTerrainClimate){0};
    if (r>0) for (size_t k=0;k<3;++k) q[k]=p[k]/r*t->radius;
    if (t->relief>0) {
        if (t->kind==AX_TERRAIN_WAVES) {
            height=t->relief;
            for (size_t k=0;k<3;++k) height*=sin(p[k]*t->frequency+t->phase[k]);
        } else if (t->kind==AX_TERRAIN_CONTINENTAL) {
            height=continental_height(t,q,climate);
        } else {
            double basin=smooth(-0.12,0.22,fractal(&t->broad,q,5/t->radius,3,0));
            double highlands=fractal(&t->detail,q,240/t->radius,3,1);
            double base=-0.16+0.32*basin+0.12*basin*(highlands-0.5);
            height=t->relief*(base+craters(t,q,24/t->radius)*0.48+craters(t,q,110/t->radius)*0.24+
                craters(t,q,520/t->radius)*0.08+fractal(&t->detail,q,t->frequency,3,0)*0.018);
        }
    }
    if (climate) {
        climate->elevation=height;
        climate->temperature=fmax(0,fmin(1,1-(r>0 ? fabs(p[1])/r : 0)-fmax(height,0)/fmax(t->relief,1)*1.15));
        double latitude=r>0 ? fabs(p[1])/r : 0;
        double dry_belt=exp(-pow((latitude-0.48)/0.15,2));
        climate->moisture=fmax(0,fmin(1,0.58+0.65*fractal(&t->broad,q,7/t->radius,2,0)-0.32*dry_belt+0.12*climate->valley));
        climate->cover=t->kind==AX_TERRAIN_CONTINENTAL ? 0.5+0.5*fractal(&t->detail,q,1600/fmax(t->radius,100000),2,0) : 0.5;
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
double ax_terrain_density(const AxTerrain *t,const double p[3],bool caves,AxTerrainClimate *climate)
{
    if (!valid_position(t,p)) { if (climate) *climate=(AxTerrainClimate){0}; return INFINITY; }
    double r=hypot(hypot(p[0],p[1]),p[2]);
    double field=r-t->radius-height_at(t,p,r,climate);
    if (caves && t->cave_depth>0 && field<-3 && field>-t->cave_depth-fmax(8,t->cave_depth*0.25)) {
        double scale=1/fmax(32,t->cave_depth);
        double a=fractal(&t->landforms,p,scale,2,0),b=fractal(&t->detail,p,scale,2,0);
        double tube=(hypot(a,b)-0.045)/scale;
        double depth_gate=fmax(field+6,-field-t->cave_depth);
        double blend=smooth(-t->cave_depth-fmax(8,t->cave_depth*0.25),-t->cave_depth,field);
        field=fmax(field,field+blend*(-fmax(tube,depth_gate)-field));
    }
    return field;
}
