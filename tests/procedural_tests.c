#include "math/noise.h"
#include "world/gravity.h"
#include "world/camera.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(c) do { if (!(c)) { fprintf(stderr,"%d: %s\n",__LINE__,#c); return false; } } while (0)
/** Verifies deterministic noise, bounded composition, negative lattices and analytic derivatives. */
static bool noise_tests(void)
{
    AxNoise a,b,c;
    ax_noise_init(&a,42); ax_noise_init(&b,42); ax_noise_init(&c,917);
    CHECK(!memcmp(&a,&b,sizeof(a)));
    bool different=false;
    for (int i=-30;i<=30;++i) {
        double p[3]={i*0.137+0.01,i*0.311-1.9,i*0.051+13.7};
        AxNoiseSample n=ax_noise_gradient3(&a,p);
        CHECK(n.value>=-1 && n.value<=1 && n.value==ax_noise_gradient3(&b,p).value);
        different|=n.value!=ax_noise_gradient3(&c,p).value;
        for (size_t k=0;k<3;++k) {
            double x[3]={p[0],p[1],p[2]},y[3]={p[0],p[1],p[2]}; x[k]+=1e-5; y[k]-=1e-5;
            double derivative=(ax_noise_gradient3(&a,x).value-ax_noise_gradient3(&a,y).value)/2e-5;
            CHECK(fabs(derivative-n.gradient[k])<1e-6);
        }
        double erosion=ax_noise_erosion3(&a,p,5);
        CHECK(erosion>=0 && erosion<=1 && erosion==ax_noise_erosion3(&b,p,5));
        double f=ax_noise_fractal3(&a,p,6,2.03,0.5,0),r=ax_noise_fractal3(&a,p,6,2.03,0.5,1);
        CHECK(f>=-1 && f<=1 && r>=0 && r<=1);
        CHECK(ax_noise_value3(&a,p)>=-1 && ax_noise_value3(&a,p)<=1);
        CHECK(isfinite(ax_noise_cellular3(&a,p)));
        double warped[3]; ax_noise_warp3(&a,p,0.5,warped);
        for (size_t k=0;k<3;++k) CHECK(fabs(warped[k]-p[k])<=0.5);
    }
    CHECK(different && ax_noise_gradient3(&a,(double[3]){NAN,0,0}).value==0);
    for (size_t k=0;k<3;++k) {
        double p[3]={-1,2.27,-5.13},q[3]={-1,2.27,-5.13}; p[k]=1-1e-8; q[k]=1+1e-8;
        CHECK(fabs(ax_noise_cellular3(&a,p)-ax_noise_cellular3(&a,q))<1e-6);
    }
    return true;
}
/** Confirms seed-dependent planetary relief, seamless spherical sampling, climate and conservative edited-volume bounds. */
static bool terrain_tests(void)
{
    AxScene scene={0};
    AxPlanetDesc d={.id=1,.seed=42,.radius=1000000,.terrain_height=6500,.terrain_frequency=0.005,
        .terrain_kind=AX_TERRAIN_CONTINENTAL,.cave_depth=128,.surface_gravity=9.81};
    CHECK(ax_scene_add_planet(&scene,&d));
    const AxPlanet *planet=ax_scene_planet(&scene,1);
    double low=INFINITY,high=-INFINITY;
    for (unsigned i=0;i<128;++i) {
        double y=1-2*(i+0.5)/128,angle=i*2.399963229728653;
        double direction[3]={sqrt(1-y*y)*cos(angle),y,sqrt(1-y*y)*sin(angle)};
        double p[3],q[3];
        for (size_t k=0;k<3;++k) { p[k]=direction[k]*d.radius; q[k]=p[k]*2; }
        double height=ax_planet_terrain_height(planet,p);
        CHECK(fabs(height)<=d.terrain_height);
        CHECK(fabs(height-ax_planet_terrain_height(planet,q))<1e-7);
        low=fmin(low,height); high=fmax(high,height);
        for (size_t k=0;k<3;++k) p[k]=direction[k]*(d.radius+height);
        AxPosition origin;
        CHECK(ax_position_from_meters(&origin,p));
        CHECK(ax_position_move(&origin,(double[3]){-8,-8,-8}));
        double minimum,maximum;
        CHECK(ax_planet_density_bounds(planet,&origin,16,true,&minimum,&maximum));
        for (unsigned j=0;j<8;++j) {
            AxPosition point=origin;
            CHECK(ax_position_move(&point,(double[3]){(j&1)*16,((j>>1)&1)*16,((j>>2)&1)*16}));
            double density=ax_planet_sample(planet,&point,NULL);
            CHECK(density>=minimum-1e-6 && density<=maximum+1e-6);
        }
    }
    CHECK(high-low>4000 && low<0 && high>3000);
    unsigned temperate=0,temperate_land=0,polar=0,polar_land=0,land=0;
    for (unsigned i=0;i<2048;++i) {
        double y=1-2*(i+0.5)/2048,angle=i*2.399963229728653;
        double p[3]={sqrt(1-y*y)*cos(angle)*d.radius,y*d.radius,sqrt(1-y*y)*sin(angle)*d.radius};
        bool dry=ax_planet_terrain_height(planet,p)>0;
        land+=dry;
        if (fabs(y)<0.5) { ++temperate; temperate_land+=dry; }
        if (fabs(y)>0.75) { ++polar; polar_land+=dry; }
    }
    CHECK(land>2048/3 && land<2048*2/3);
    CHECK((double)temperate_land/temperate>(double)polar_land/polar+0.1);
    d.id=2; d.seed=917; d.terrain_kind=AX_TERRAIN_CRATERED; d.cave_depth=0;
    const AxPlanet *moon=ax_scene_add_planet(&scene,&d);
    CHECK(moon && ax_planet_terrain_height(moon,(double[3]){0,d.radius,0})!=ax_planet_terrain_height(planet,(double[3]){0,d.radius,0}));
    /* Sample local traverses as well as globe-scale relief on both bodies. */
    for (unsigned body=0;body<2;++body) {
        const AxPlanet *sample=body ? moon : planet;
        double largest=0;
        for (unsigned site=0;site<24;++site) {
            double angle=site*2.399963229728653,y=-0.8+1.6*site/23;
            double direction[3]={sqrt(1-y*y)*cos(angle),y,sqrt(1-y*y)*sin(angle)};
            double local_low=INFINITY,local_high=-INFINITY;
            for (unsigned step=0;step<33;++step) {
                double p[3]={direction[0]*d.radius+(step-16.0)*64,direction[1]*d.radius,direction[2]*d.radius};
                double h=ax_planet_terrain_height(sample,p);
                CHECK(isfinite(h) && fabs(h)<=d.terrain_height);
                local_low=fmin(local_low,h); local_high=fmax(local_high,h);
                p[0]+=0.001;
                CHECK(fabs(h-ax_planet_terrain_height(sample,p))<0.1);
            }
            largest=fmax(largest,local_high-local_low);
        }
        CHECK(largest>50);
    }
    AxVolumeEdit edit={.half_extent={12,12,12},.remove=true,.material=AX_MATERIAL_ROCK};
    CHECK(ax_position_from_meters(&edit.position,(double[3]){0,d.radius,0}));
    CHECK(ax_scene_edit(&scene,1,&edit));
    double minimum,maximum;
    CHECK(ax_planet_density_bounds(planet,&edit.position,32,true,&minimum,&maximum));
    CHECK(minimum<=maximum);
    ax_scene_destroy(&scene);
    return true;
}
/** Checks that a representative 120 km province contains gentler valley floors, alpine relief and shallow shelves. */
static bool region_tests(void)
{
    AxTerrain terrain;
    CHECK(ax_terrain_init(&terrain,AX_TERRAIN_CONTINENTAL,42,1000000,10000,0.005,0));
    double radial[3]={-0.15211999,0.05957031,-0.9865652},east[3]={-radial[2],0,radial[0]},north[3];
    double length=hypot(east[0],east[2]);
    for (size_t k=0;k<3;++k) east[k]/=length;
    north[0]=radial[1]*east[2]-radial[2]*east[1];
    north[1]=radial[2]*east[0]-radial[0]*east[2];
    north[2]=radial[0]*east[1]-radial[1]*east[0];
    unsigned floors=0,ridges=0,shelves=0;
    double floor_slope=0,ridge_slope=0,low=INFINITY,high=-INFINITY;
    for (unsigned z=0;z<64;++z) for (unsigned x=0;x<64;++x) {
        double p[3],a[3],b[3];
        for (size_t k=0;k<3;++k) {
            p[k]=radial[k]*terrain.radius+east[k]*(-60000+x*120000.0/63)+north[k]*(-60000+z*120000.0/63);
            a[k]=p[k]+east[k]*25; b[k]=p[k]+north[k]*25;
        }
        AxTerrainClimate climate;
        double height=ax_terrain_height(&terrain,p,&climate);
        double slope=hypot(ax_terrain_height(&terrain,a,NULL)-height,ax_terrain_height(&terrain,b,NULL)-height)/25;
        CHECK(isfinite(height) && fabs(height)<=terrain.relief);
        CHECK(climate.valley>=0 && climate.valley<=1 && climate.cover>=0 && climate.cover<=1);
        low=fmin(low,height); high=fmax(high,height);
        if (climate.valley>0.99 && height>0) { ++floors; floor_slope+=slope; }
        if (climate.valley<0.2 && height>1000) { ++ridges; ridge_slope+=slope; }
        if (height>-400 && height<-5) ++shelves;
    }
    CHECK(floors>50 && ridges>100 && shelves>50 && high-low>7000);
    CHECK(floor_slope/floors<0.25 && floor_slope/floors<ridge_slope/ridges*0.5);
    return true;
}
/** Checks physical acceleration, cancellation, smooth flight influence and parallel transport through poles. */
static bool gravity_tests(void)
{
    AxScene scene={0};
    AxPlanetDesc d={.id=1,.radius=100,.surface_gravity=10};
    CHECK(ax_scene_add_planet(&scene,&d));
    AxPosition p;
    AxGravity gravity;
    CHECK(ax_position_from_meters(&p,(double[3]){0,100,0}) && ax_gravity_sample(&scene,1,&p,&gravity));
    CHECK(fabs(gravity.acceleration[1]+10)<1e-10 && gravity.up[1]==1 && gravity.alignment==1);
    CHECK(ax_position_from_meters(&p,(double[3]){0,200,0}) && ax_gravity_sample(&scene,1,&p,&gravity));
    CHECK(fabs(gravity.strength-2.5)<1e-10);
    CHECK(ax_position_from_meters(&p,(double[3]){0,50,0}) && ax_gravity_sample(&scene,1,&p,&gravity));
    CHECK(fabs(gravity.strength-5)<1e-10);
    p=(AxPosition){0}; CHECK(ax_gravity_sample(&scene,1,&p,&gravity) && gravity.strength==0 && gravity.alignment==0);
    CHECK(ax_position_from_meters(&p,(double[3]){0,1e6,0}) && ax_gravity_sample(&scene,1,&p,&gravity) && gravity.alignment==0);
    d.id=2; d.center[0]=400;
    CHECK(ax_scene_add_planet(&scene,&d));
    CHECK(ax_position_from_meters(&p,(double[3]){200,0,0}) && ax_gravity_sample(&scene,1,&p,&gravity));
    CHECK(gravity.strength<1e-12 && gravity.alignment==0);
    AxCamera camera;
    ax_camera_init(&camera,NULL,0,0,NULL);
    for (unsigned i=0;i<720;++i) {
        double angle=i*6.283185307179586/360,up[3]={sin(angle),cos(angle),0};
        ax_camera_align(&camera,up,1,1.0/60);
        float f[3],r[3],u[3]; ax_camera_basis(&camera,f,r,u);
        double lengths[3]={0},dot=0;
        for (size_t k=0;k<3;++k) { lengths[0]+=f[k]*f[k]; lengths[1]+=r[k]*r[k]; lengths[2]+=u[k]*u[k]; dot+=f[k]*r[k]; }
        CHECK(fabs(dot)<1e-5 && fabs(lengths[0]-1)<1e-5 && fabs(lengths[1]-1)<1e-5 && fabs(lengths[2]-1)<1e-5);
    }
    AxCamera previous=camera;
    ax_camera_align(&camera,(double[3]){-1,0,0},0,1);
    CHECK(!memcmp(&camera,&previous,sizeof(camera)));
    for (unsigned i=0;i<120;++i) ax_camera_align(&camera,(double[3]){1,0,0},1,1.0/60);
    AxInput input={0}; ax_input_key(&input,AX_KEY_E,true,false,0);
    ax_camera_update(&camera,&input,0.1);
    CHECK(camera.position[0]>0.59 && fabs(camera.position[1])<0.001);
    ax_scene_destroy(&scene);
    return true;
}
/** Runs reusable noise, volume-bound and gravity-aware flight regressions. */
int main(void) { return noise_tests() && terrain_tests() && region_tests() && gravity_tests() ? EXIT_SUCCESS : EXIT_FAILURE; }
