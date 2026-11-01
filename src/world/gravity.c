#include "gravity.h"
#include <math.h>
#include <stddef.h>
bool ax_gravity_sample(const AxScene *scene,AxPlanetId frame,const AxPosition *position,AxGravity *gravity)
{
    const AxPlanet *anchor=ax_scene_planet(scene,frame);
    if (!anchor || !position || !gravity) return false;
    AxPosition normalized=*position;
    if (!ax_position_normalize(&normalized)) return false;
    const AxPlanetDesc *reference=ax_planet_description(anchor);
    AxGravity sample={0};
    double strongest=0;
    for (size_t i=0;i<scene->count;++i) {
        const AxPlanetDesc *body=ax_planet_description(scene->planets[i]);
        if (body->surface_gravity==0) continue;
        AxPosition local=normalized;
        double shift[3];
        for (size_t k=0;k<3;++k) shift[k]=reference->center[k]-body->center[k];
        if (!ax_position_move(&local,shift)) return false;
        const AxPosition zero={0};
        double p[3];
        ax_position_difference(&local,&zero,p);
        double distance=hypot(hypot(p[0],p[1]),p[2]);
        if (distance==0) continue;
        double ratio=distance<body->radius ? distance/body->radius : body->radius/distance;
        double strength=body->surface_gravity*(distance<body->radius ? ratio : ratio*ratio);
        for (size_t k=0;k<3;++k) sample.acceleration[k]-=p[k]/distance*strength;
        if (strength>strongest) { strongest=strength; sample.dominant=body->id; }
    }
    sample.strength=hypot(hypot(sample.acceleration[0],sample.acceleration[1]),sample.acceleration[2]);
    if (sample.strength>1e-12) {
        for (size_t k=0;k<3;++k) sample.up[k]=-sample.acceleration[k]/sample.strength;
        double t=fmax(0,fmin(1,(sample.strength-0.02)/0.23));
        sample.alignment=t*t*(3-2*t);
    }
    *gravity=sample;
    return true;
}
