/*
Copyright (C) 1997-2001 Id Software, Inc.

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  

See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.

*/
// r_misc.c

#include "r_local.h"

#define NUM_MIPS	4

cvar_t	*sw_mipcap;
cvar_t	*sw_mipscale;

surfcache_t		*d_initial_rover;
qboolean		d_roverwrapped;
int				d_minmip;
float			d_scalemip[NUM_MIPS-1];

static float	basemip[NUM_MIPS-1] = {1.0, 0.5*0.8, 0.25*0.8};

extern int			d_aflatcolor;

int	d_vrectx, d_vrecty, d_vrectright_particle, d_vrectbottom_particle;

int	d_pix_min, d_pix_max, d_pix_shift;

int		d_scantable[MAXHEIGHT];
short	*zspantable[MAXHEIGHT]; 

/*
================
D_Patch
================
*/
void D_Patch (void)
{
#if id386
	extern void D_Aff8Patch( void );
	static qboolean protectset8 = false;
	extern void D_PolysetAff8Start( void );

	if (!protectset8)
	{
		Sys_MakeCodeWriteable ((int)D_PolysetAff8Start,
						     (int)D_Aff8Patch - (int)D_PolysetAff8Start);
		Sys_MakeCodeWriteable ((long)R_Surf8Start,
						 (long)R_Surf8End - (long)R_Surf8Start);
		protectset8 = true;
	}
	colormap = vid.colormap;

	R_Surf8Patch ();
	D_Aff8Patch();
#endif
}
/*
================
D_ViewChanged
================
*/
unsigned char *alias_colormap;

void D_ViewChanged (void)
{
	int		i;

	scale_for_mip = xscale;
	if (yscale > xscale)
		scale_for_mip = yscale;

	d_zrowbytes = vid.width * 2;
	d_zwidth = vid.width;

	d_pix_min = gpGlobals->width / 320;
	if (d_pix_min < 1)
		d_pix_min = 1;

	d_pix_max = (int)((float)gpGlobals->height / (320.0 / 4.0) + 0.5);
	d_pix_shift = 8 - (int)((float)gpGlobals->height / 320.0 + 0.5);
	if (d_pix_max < 1)
		d_pix_max = 1;

	d_vrectx = RI.vrect.x;
	d_vrecty = RI.vrect.y;
	d_vrectright_particle = gpGlobals->width - d_pix_max;
	d_vrectbottom_particle =
			gpGlobals->height - d_pix_max;

	for (i=0 ; i<vid.height; i++)
	{
		d_scantable[i] = i*r_screenwidth;
		zspantable[i] = d_pzbuffer + i*d_zwidth;
	}

	/*
	** clear Z-buffer and color-buffers if we're doing the gallery
	*/
	if ( !RI.drawWorld )
	{
		memset( d_pzbuffer, 0xff, vid.width * vid.height * sizeof( d_pzbuffer[0] ) );
		// newrefdef
		Draw_Fill( 0, 0, gpGlobals->width, gpGlobals->height,( int ) sw_clearcolor->value & 0xff );
	}

	alias_colormap = vid.colormap;

	D_Patch ();
}



/*
===================
R_TransformFrustum
===================
*/
void R_TransformFrustum (void)
{
	int		i;
	vec3_t	v, v2;
	
	for (i=0 ; i<4 ; i++)
	{
		v[0] = screenedge[i].normal[2];
		v[1] = -screenedge[i].normal[0];
		v[2] = screenedge[i].normal[1];

		v2[0] = v[1]*vright[0] + v[2]*vup[0] + v[0]*vpn[0];
		v2[1] = v[1]*vright[1] + v[2]*vup[1] + v[0]*vpn[1];
		v2[2] = v[1]*vright[2] + v[2]*vup[2] + v[0]*vpn[2];

		VectorCopy (v2, view_clipplanes[i].normal);

		view_clipplanes[i].dist = DotProduct (modelorg, v2);
	}
}


/*
================
TransformVector
================
*/
void TransformVector (vec3_t in, vec3_t out)
{
	out[0] = DotProduct(in,vright);
	out[1] = DotProduct(in,vup);
	out[2] = DotProduct(in,vpn);		
}

/*
================
R_TransformPlane
================
*/
void R_TransformPlane (mplane_t *p, float *normal, float *dist)
{
	float	d;
	
	d = DotProduct (RI.vieworg, p->normal);
	*dist = p->dist - d;
// TODO: when we have rotating entities, this will need to use the view matrix
	TransformVector (p->normal, normal);
}


/*
===============
R_SetUpFrustumIndexes
===============
*/
void R_SetUpFrustumIndexes (void)
{
	int		i, j, *pindex;

	pindex = r_frustum_indexes;

	for (i=0 ; i<4 ; i++)
	{
		for (j=0 ; j<3 ; j++)
		{
			if (view_clipplanes[i].normal[j] < 0)
			{
				pindex[j] = j;
				pindex[j+3] = j+3;
			}
			else
			{
				pindex[j] = j+3;
				pindex[j+3] = j;
			}
		}

	// FIXME: do just once at start
		pfrustum_indexes[i] = pindex;
		pindex += 6;
	}
}

/*
===============
R_ViewChanged

Called every time the vid structure or r_refdef changes.
Guaranteed to be called before the first refresh
===============
*/
void R_ViewChanged (vrect_t *vr)
{
	int		i;
	float verticalFieldOfView, xOrigin, yOrigin;

	RI.vrect = *vr;

	RI.horizontalFieldOfView = 2*tan((float)RI.fov_x/360*M_PI);
	verticalFieldOfView = 2*tan((float)RI.fov_y/360*M_PI);

	RI.fvrectx = (float)RI.vrect.x;
	RI.fvrectx_adj = (float)RI.vrect.x - 0.5;
	RI.vrect_x_adj_shift20 = (RI.vrect.x<<20) + (1<<19) - 1;
	RI.fvrecty = (float)RI.vrect.y;
	RI.fvrecty_adj = (float)RI.vrect.y - 0.5;
	RI.vrectright = RI.vrect.x + RI.vrect.width;
	RI.vrectright_adj_shift20 = (RI.vrectright<<20) + (1<<19) - 1;
	RI.fvrectright = (float)RI.vrectright;
	RI.fvrectright_adj = (float)RI.vrectright - 0.5;
	RI.vrectrightedge = (float)RI.vrectright - 0.99;
	RI.vrectbottom = RI.vrect.y + RI.vrect.height;
	RI.fvrectbottom = (float)RI.vrectbottom;
	RI.fvrectbottom_adj = (float)RI.vrectbottom - 0.5;

	//RI.aliasvrect.x = (int)(RI.vrect.x * r_aliasuvscale);
	//RI.aliasvrect.y = (int)(RI.vrect.y * r_aliasuvscale);
	//RI.aliasvrect.width = (int)(RI.vrect.width * r_aliasuvscale);
	//RI.aliasvrect.height = (int)(RI.vrect.height * r_aliasuvscale);
	RI.aliasvrectright = RI.aliasvrect.x +
			RI.aliasvrect.width;
	RI.aliasvrectbottom = RI.aliasvrect.y +
			RI.aliasvrect.height;

	xOrigin = RI.xOrigin;// = r_origin[0];
	yOrigin = RI.yOrigin;// = r_origin[1];
#define PLANE_ANYZ 5
// values for perspective projection
// if math were exact, the values would range from 0.5 to to range+0.5
// hopefully they wll be in the 0.000001 to range+.999999 and truncate
// the polygon rasterization will never render in the first row or column
// but will definately render in the [range] row and column, so adjust the
// buffer origin to get an exact edge to edge fill
	xcenter = ((float)RI.vrect.width * XCENTERING) +
					RI.vrect.x - 0.5;
	//aliasxcenter = xcenter * r_aliasuvscale;
	ycenter = ((float)RI.vrect.height * YCENTERING) +
					RI.vrect.y - 0.5;
//	aliasycenter = ycenter * r_aliasuvscale;

	xscale = RI.vrect.width / RI.horizontalFieldOfView;
//	aliasxscale = xscale * r_aliasuvscale;
	xscaleinv = 1.0 / xscale;

	yscale = xscale;
//	aliasyscale = yscale * r_aliasuvscale;
	yscaleinv = 1.0 / yscale;
	xscaleshrink = (RI.vrect.width-6)/RI.horizontalFieldOfView;
	yscaleshrink = xscaleshrink;

// left side clip
	screenedge[0].normal[0] = -1.0 / (xOrigin*RI.horizontalFieldOfView);
	screenedge[0].normal[1] = 0;
	screenedge[0].normal[2] = 1;
	screenedge[0].type = PLANE_ANYZ;

// right side clip
	screenedge[1].normal[0] =
					1.0 / ((1.0-xOrigin)*RI.horizontalFieldOfView);
	screenedge[1].normal[1] = 0;
	screenedge[1].normal[2] = 1;
	screenedge[1].type = PLANE_ANYZ;

// top side clip
	screenedge[2].normal[0] = 0;
	screenedge[2].normal[1] = -1.0 / (yOrigin*verticalFieldOfView);
	screenedge[2].normal[2] = 1;
	screenedge[2].type = PLANE_ANYZ;

// bottom side clip
	screenedge[3].normal[0] = 0;
	screenedge[3].normal[1] = 1.0 / ((1.0-yOrigin)*verticalFieldOfView);
	screenedge[3].normal[2] = 1;
	screenedge[3].type = PLANE_ANYZ;

	for (i=0 ; i<4 ; i++)
			VectorNormalize (screenedge[i].normal);

	D_ViewChanged ();
}


/*
===============
R_SetupFrame
===============
*/
void R_SetupFrameQ (void)
{
	int			i;
	vrect_t		vrect;

	if (r_fullbright->flags & FCVAR_CHANGED)
	{
		r_fullbright->flags &= ~FCVAR_CHANGED;
		D_FlushCaches( false );	// so all lighting changes
	}
	
	r_framecount++;


// build the transformation matrix for the given view angles
	VectorCopy (RI.vieworg, modelorg);
	VectorCopy (RI.vieworg, r_origin);

	AngleVectors (RI.viewangles, vpn, vright, vup);

// current viewleaf
	if ( RI.drawWorld )
	{
		r_viewleaf = gEngfuncs.Mod_PointInLeaf (r_origin, WORLDMODEL->nodes);
		r_viewcluster = r_viewleaf->cluster;
	}

//	if (sw_waterwarp->value && (r_newrefdef.rdflags & RDF_UNDERWATER) )
//		r_dowarp = true;
//	else
		r_dowarp = false;

	if (r_dowarp)
	{	// warp into off screen buffer
		vrect.x = 0;
		vrect.y = 0;
		//vrect.width = r_newrefdef.width < WARP_WIDTH ? r_newrefdef.width : WARP_WIDTH;
		//vrect.height = r_newrefdef.height < WARP_HEIGHT ? r_newrefdef.height : WARP_HEIGHT;

		d_viewbuffer = r_warpbuffer;
		r_screenwidth = WARP_WIDTH;
	}
	else
	{
		vrect.x = 0;//r_newrefdef.x;
		vrect.y = 0;//r_newrefdef.y;
		vrect.width = gpGlobals->width;
		vrect.height = gpGlobals->height;

		d_viewbuffer = (void *)vid.buffer;
		r_screenwidth = vid.rowbytes;
	}
	
	R_ViewChanged (&vrect);

// start off with just the four screen edge clip planes
	R_TransformFrustum ();
	R_SetUpFrustumIndexes ();

// save base values
	VectorCopy (vpn, base_vpn);
	VectorCopy (vright, base_vright);
	VectorCopy (vup, base_vup);

// clear frame counts
/*	c_faceclip = 0;
	d_spanpixcount = 0;
	r_polycount = 0;
	r_drawnpolycount = 0;
	r_wholepolycount = 0;
	r_amodels_drawn = 0;
	r_outofsurfaces = 0;
	r_outofedges = 0;*/

// d_setup
	d_roverwrapped = false;
	d_initial_rover = sc_rover;

	d_minmip = sw_mipcap->value;
	if (d_minmip > 3)
		d_minmip = 3;
	else if (d_minmip < 0)
		d_minmip = 0;

	for (i=0 ; i<(NUM_MIPS-1) ; i++)
		d_scalemip[i] = basemip[i] * sw_mipscale->value;

	//d_aflatcolor = 0;
}


#if	!id386

/*
================
R_SurfacePatch
================
*/
/*void R_SurfacePatch (void)
{
	// we only patch code on Intel
}
*/
#endif	// !id386