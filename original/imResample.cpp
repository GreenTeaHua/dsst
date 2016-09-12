
#include "DSSTSettings.h"
#include "string.h"
#include <math.h>
#include <typeinfo>
#include <malloc.h>

#ifdef SSEv2 
  #include "sse.hpp"
#endif

//compute interpolation values for single column for resapling
template<class T> void resampleCoef( int ha, int hb, int &n, int *&yas,
  int *&ybs, T *&wts, int bd[2], int pad=0 )
{
  const T s = T(hb)/T(ha), sInv = 1/s; T wt, wt0=T(1e-3)*s;
  bool ds=ha>hb; int nMax; bd[0]=bd[1]=0;
  if(ds) { n=0; nMax=ha+(pad>2 ? pad : 2)*hb; } else { n=nMax=hb; }

#ifdef SSEv2
  wts = (T*)_aligned_malloc(nMax*sizeof(T),16);
  yas = (int*)_aligned_malloc(nMax*sizeof(int),16);
  ybs = (int*)_aligned_malloc(nMax*sizeof(int),16);
#else
  wts = new T [nMax];
  yas = new int [nMax];
  ybs = new int [nMax];
#endif

  if( ds ) for( int yb=0; yb<hb; yb++ ) {
    // create coefficients for downsampling
    T ya0f=yb*sInv, ya1f=ya0f+sInv, W=0;
    int ya0=int(ceil(ya0f)), ya1=int(ya1f), n1=0;
    for( int ya=ya0-1; ya<ya1+1; ya++ ) {
      wt=s; if(ya==ya0-1) wt=(ya0-ya0f)*s; else if(ya==ya1) wt=(ya1f-ya1)*s;
      if(wt>wt0 && ya>=0) { ybs[n]=yb; yas[n]=ya; wts[n]=wt; n++; n1++; W+=wt; }
    }
    if(W>1) for( int i=0; i<n1; i++ ) wts[n-n1+i]/=W;
    if(n1>bd[0]) bd[0]=n1;
    while( n1<pad ) { ybs[n]=yb; yas[n]=yas[n-1]; wts[n]=0; n++; n1++; }
  } else for( int yb=0; yb<hb; yb++ ) {
    // create coefficients for upsampling
    T yaf = (T(.5)+yb)*sInv-T(.5); int ya=(int) floor(yaf);
    wt=1; if(ya>=0 && ya<ha-1) wt=1-(yaf-ya);
    if(ya<0) { ya=0; bd[0]++; } if(ya>=ha-1) { ya=ha-1; bd[1]++; }
    ybs[yb]=yb; yas[yb]=ya; wts[yb]=wt;
  }
}

// resample A using bilinear interpolation and and store result in B
template<class T>
void resample( T *A, T *B, int ha, int hb, int wa, int wb, int d, T r ) {
	int hn, wn, x, x1, y, z, xa, xb, ya; T *A0, *A1, *A2, *A3, *B0, wt, wt1;
#ifdef SSEv2
	T *C = (T*) _aligned_malloc((ha+4)*sizeof(T),16); 
	//�������������� �������� ������������ �� SSE
	bool sse = (typeid(T)==typeid(float)) && !(size_t(A)&15) && !(size_t(B)&15);
#else
	T *C = new T [ha+4]; 
#endif
	for(y=ha; y<ha+4; y++) C[y]=0;
	//bool sse = (typeid(T)==typeid(float)) && !(size_t(A)&15) && !(size_t(B)&15);
	// get coefficients for resampling along w and h
	int *xas, *xbs, *yas, *ybs; T *xwts, *ywts; int xbd[2], ybd[2];
	resampleCoef<T>( wa, wb, wn, xas, xbs, xwts, xbd, 0 );
	resampleCoef<T>( ha, hb, hn, yas, ybs, ywts, ybd, 4 );
	if( wa==2*wb ) r/=2; if( wa==3*wb ) r/=3; if( wa==4*wb ) r/=4;
	r/=T(1+1e-6); for( y=0; y<hn; y++ ) ywts[y] *= r;
	// resample each channel in turn
	for( z=0; z<d; z++ ) for( x=0; x<wb; x++ ) {
		if(x==0) x1=0; xa=xas[x1]; xb=xbs[x1]; wt=xwts[x1]; wt1=1-wt; y=0;
		A0=A+z*ha*wa+xa*ha; A1=A0+ha, A2=A1+ha, A3=A2+ha; B0=B+z*hb*wb+xb*hb;
		// variables for SSE (simple casts to float)
		float *Af0, *Af1, *Af2, *Af3, *Bf0, *Cf, *ywtsf, wtf, wt1f;
		Af0=(float*) A0; Af1=(float*) A1; Af2=(float*) A2; Af3=(float*) A3;
		Bf0=(float*) B0; Cf=(float*) C;
		ywtsf=(float*) ywts; wtf=(float) wt; wt1f=(float) wt1;
		// resample along x direction (A -> C)
#ifdef SSEv2
        #define FORs(X) if(sse) for(; y<ha-4; y+=4) STR(Cf[y],X);
#else
        #define FORs(X) for(; y<ha-4; y++) Cf[y] = X;
#endif
        #define FORr(X) for(; y<ha; y++) C[y] = X;
		if( wa==2*wb ) {
#ifdef SSEv2
			FORs( ADD(LDu(Af0[y]), LDu(Af1[y])) );
#else
			FORs( Af0[y]+Af1[y] );
#endif
			FORr( A0[y]+A1[y] ); x1+=2;
		} else if( wa==3*wb ) {

#ifdef SSEv2
			FORs( ADD(LDu(Af0[y]),LDu(Af1[y]),LDu(Af2[y])) );
#else
			FORs( Af0[y] + Af1[y] + Af2[y] );
#endif 
			FORr( A0[y]+A1[y]+A2[y] ); x1+=3;
		} else if( wa==4*wb ) {
#ifdef SSEv2
			FORs( ADD(LDu(Af0[y]),LDu(Af1[y]),LDu(Af2[y]),LDu(Af3[y])) );
#else
			FORs( Af0[y]+Af1[y]+Af2[y]+Af3[y] );
#endif 
			FORr( A0[y]+A1[y]+A2[y]+A3[y] ); x1+=4;
		} else if( wa>wb ) {
			int m=1; while( x1+m<wn && xb==xbs[x1+m] ) m++; float wtsf[4];
			for( int x0=0; x0<(m<4?m:4); x0++ ) wtsf[x0]=float(xwts[x1+x0]);

#ifdef SSEv2
            #define U(x) MUL( LDu(*(Af ## x + y)), SET(wtsf[x]) )
            #define V(x) *(A ## x + y) * xwts[x1+x]
			if(m==1) { FORs(U(0));                     FORr(V(0)); }
			if(m==2) { FORs(ADD(U(0),U(1)));           FORr(V(0)+V(1)); }
			if(m==3) { FORs(ADD(U(0),U(1),U(2)));      FORr(V(0)+V(1)+V(2)); }
			if(m>=4) { FORs(ADD(U(0),U(1),U(2),U(3))); FORr(V(0)+V(1)+V(2)+V(3)); }
#else
            #define U(x) *(Af ## x + y) * wtsf[x] 
            #define V(x) *(A ## x + y) * xwts[x1+x]
			if(m==1) { FORs(U(0));                FORr(V(0)); }
			if(m==2) { FORs(U(0)+U(1));           FORr(V(0)+V(1)); }
			if(m==3) { FORs(U(0)+U(1)+U(2));      FORr(V(0)+V(1)+V(2)); }
			if(m>=4) { FORs(U(0)+U(1)+U(2)+U(3)); FORr(V(0)+V(1)+V(2)+V(3)); }
#endif 
#undef U
#undef V
			for( int x0=4; x0<m; x0++ ) {
				A1=A0+x0*ha; wt1=xwts[x1+x0]; Af1=(float*) A1; wt1f=float(wt1); y=0;
#ifdef SSEv2
				FORs(ADD(LD(Cf[y]),MUL(LDu(Af1[y]),SET(wt1f)))); 
#else
				FORs(Cf[y] + Af1[y] * wt1f); 
#endif 
				FORr(C[y]+A1[y]*wt1);
			}
			x1+=m;
		} else {
			bool xBd = x<xbd[0] || x>=wb-xbd[1]; x1++;
			if(xBd) memcpy(C,A0,ha*sizeof(T));
#ifdef SSEv2
			if(!xBd) FORs(ADD(MUL(LDu(Af0[y]),SET(wtf)),MUL(LDu(Af1[y]),SET(wt1f))));
#else
			if(!xBd) FORs( Af0[y]*wtf + Af1[y]*wt1f);
#endif 
			if(!xBd) FORr( A0[y]*wt + A1[y]*wt1 );
		}
#undef FORs
#undef FORr
		// resample along y direction (B -> C)
		if( ha==hb*2 ) {
			T r2 = r/2; int k=((~((size_t) B0) + 1) & 15)/4; y=0;
			for( ; y<k; y++ )  B0[y]=(C[2*y]+C[2*y+1])*r2;
#ifdef SSEv2
			if(sse) for(; y<hb-4; y+=4) STR(Bf0[y],MUL((float)r2,_mm_shuffle_ps(ADD(
			LDu(Cf[2*y]),LDu(Cf[2*y+1])),ADD(LDu(Cf[2*y+4]),LDu(Cf[2*y+5])),136))); //136 = _MM_SHUFFLE(2, 0, 2, 0);
#else
			for(; y<hb-4; y+=4) {	   
				Bf0[y] = float(r2) * (Cf[2 * y] + Cf[2 * y + 1]);
				Bf0[y + 1] = float(r2) * (Cf[2 * y + 2] + Cf[2 * y + 3]);
				Bf0[y + 2] = float(r2) * (Cf[2 * y + 4] + Cf[2 * y + 5]);
				Bf0[y + 3] = float(r2) * (Cf[2 * y + 6] + Cf[2 * y + 7]);
			}
#endif      
			for( ; y<hb; y++ ) B0[y]=(C[2*y]+C[2*y+1])*r2;
		} else if( ha==hb*3 ) {
			for(y=0; y<hb; y++) B0[y]=(C[3*y]+C[3*y+1]+C[3*y+2])*(r/3);
		} else if( ha==hb*4 ) {
			for(y=0; y<hb; y++) B0[y]=(C[4*y]+C[4*y+1]+C[4*y+2]+C[4*y+3])*(r/4);
		} else if( ha>hb ) {
			y=0;
#define U(o) C[ya+o]*ywts[y*4+o]
			if(ybd[0]==2) for(; y<hb; y++) { ya=yas[y*4]; B0[y]=U(0)+U(1); }
			if(ybd[0]==3) for(; y<hb; y++) { ya=yas[y*4]; B0[y]=U(0)+U(1)+U(2); }
			if(ybd[0]==4) for(; y<hb; y++) { ya=yas[y*4]; B0[y]=U(0)+U(1)+U(2)+U(3); }
			if(ybd[0]>4)  for(; y<hn; y++) { B0[ybs[y]] += C[yas[y]] * ywts[y]; }
#undef U
		} else {
			for(y=0; y<ybd[0]; y++) B0[y] = C[yas[y]]*ywts[y];
			for(; y<hb-ybd[1]; y++) B0[y] = C[yas[y]]*ywts[y]+C[yas[y]+1]*(r-ywts[y]);
			for(; y<hb; y++)        B0[y] = C[yas[y]]*ywts[y];
		}
	}

#ifdef SSEv2
	_aligned_free(xas); _aligned_free(xbs); _aligned_free(xwts); _aligned_free(C);
	_aligned_free(yas); _aligned_free(ybs); _aligned_free(ywts);
#else
	delete[] xas; delete[] xbs; delete[] xwts; delete[] C;
	delete[] yas; delete[] ybs; delete[] ywts; 
#endif

}

template<class iT>
void imResampleWrapper(iT *In, iT *&Out, int hi, int wi, int ho, int wo, int d, double nrm)
{
  int n=hi * wi * d; int m=ho * wo * d;
  resample((float*)In, (float*)Out, hi, ho, wi, wo, d, float(nrm));
}
