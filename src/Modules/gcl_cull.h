/* gcl_cull.h — PAYLASILAN GORUNTULEME (CULLING) ALTYAPISI.

   NEDEN VAR: bu modul agacindaki hicbir cizim yolu, kameranin GORMEDIGI
   geometriyi elemiyordu. fps_first_demo'da arazi 41.454 ucgendir ve tek bir
   DrawModel ile HER KARE, kameranin arkasinda kalan ucgenler dahil, GPU'ya
   gonderiliyordu. Objeler birbirini de elemiyordu: bir dagin arkasindaki kutu,
   ekranda gorunmese bile butun vertex isleminden geciyordu.

   Bu baslik UC soruya cevap verir ve hepsi de KONSERVATIFTIR — yani "gorunmez"
   dedigi sey GERCEKTEN gorunmezdir; yanlis eleme (popping / kaybolan obje)
   uretemez:

     1. FRUSTUM (gorus hacmi). Kameranin onunde ve gorus acisinin icinde mi?
        Alti duzlemle AABB testi.
     2. MESAFE. Cok uzaktaki bir obje zaten okunamaz; FAR cull ile butun kare
        hesaplanmadan atilir.
     3. OCCLUSION (onune bir sey girmis mi?). Arazi ucgenleri, kaba (dusuk
        cozunurluklu) bir YAZILIM DERINLIK TAMPONUNA bir kez rastrelenir; sonra
        bir obje "tamamen bu derinligin ARKASINDA mi" diye sorulur. Yalnizca
        obje kutunun TAMAMI tampondaki yuzeyin arkasindaysa elenır — bir kosesi
        bile aciktaysa obje cizilir.

   Ayarlar ortam degiskeniyle kapatilabilir (tani/kiyaslama icin):
     GCL_NO_FRUSTUM=1   frustum elemeyi kapatir
     GCL_NO_OCCLUSION=1 occlusion elemeyi kapatir
   Varsayilan: ikisi de ACIK.

   Header-only (static) tutulmustur: culling'i yalnizca kendi cizim yolunda
   kullanan moduller include eder. Terrain ile Mesh AYNI modulde (SimpleMesh)
   oldugu icin occluder + sorgu tek bir translation unit'te yasar. */

#ifndef GCL_CULL_H
#define GCL_CULL_H

#include <math.h>
#include <stdlib.h>
#include <string.h>

/* ---------- ayar ---------- */

#define GCL_NEAR_PLANE   0.10f
#define GCL_FAR_PLANE    4000.0f

/* Occlusion tamponu cozunurlugu. Sahne ucgenleri buraya KABA sekilde
   rastrelenir; amac dogru bir goruntu uretmek degil, "su hucrede arazinin
   derinligi ne" sorusunu ~1 ms'de cevaplamaktir. 96x54, 16:9 icin makul bir
   denge: hucre boyu ekranda ~10-20 piksele denk gelir, ki bu bir dagin
   arkasindaki kutuyu elemek icin fazlasiyla yeterlidir. */
#define GCL_OCC_W 96
#define GCL_OCC_H 54

/* Occlusion guvenlik payi (dunya birimi). Tampon KABA oldugu icin sinirlarda
   yanlis eleme olmasin diye obje, tampondaki yuzeyden bu kadar DAHA ARKADA
   olmali. */
#define GCL_OCC_BIAS 0.05f

#define GCL_OCC_EMPTY 1.0e30f

/* ---------- kucuk vektor yardimcilari (dosya-yerel) ---------- */

static float gclc_dot3(const float a[3], const float b[3]) {
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}
static void gclc_sub3(const float a[3], const float b[3], float o[3]) {
    o[0] = a[0] - b[0]; o[1] = a[1] - b[1]; o[2] = a[2] - b[2];
}
static void gclc_cross3(const float a[3], const float b[3], float o[3]) {
    o[0] = a[1] * b[2] - a[2] * b[1];
    o[1] = a[2] * b[0] - a[0] * b[2];
    o[2] = a[0] * b[1] - a[1] * b[0];
}
static float gclc_norm3(float v[3]) {
    float l = sqrtf(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
    if (l > 1.0e-9f) { v[0] /= l; v[1] /= l; v[2] /= l; }
    return l;
}

/* ---------- frustum ---------- */

typedef struct {
    float n[3];   /* iceri bakan birim normal */
    float d;      /* ic kosulu: n . x >= d        */
} GclCullPlane;

typedef struct {
    GclCullPlane p[6];   /* near, far, left, right, bottom, top */
    float pos[3];        /* kamera goz noktasi   */
    float fwd[3], rgt[3], upv[3];  /* ortonormal taban */
    float tan_x, tan_y;  /* yarim aci tanjantlari */
    int   valid;
} GclFrustum;

/* Kameradan frustum duzlemlerini kurar.

   Kamera: goz `pos`, bakilan nokta `target`, yukari `up`, dikey gorus acisi
   `fovy_deg` DERECE, `aspect` = genislik/yukseklik. Geometrik kurulum (matris
   konvansiyonu yok): her yan duzlem kameradan gecer ve normali, o kenara
   bakan yarim aci vektorune diktir. Bu, raylib'in row-vector matris
   konvansiyonundan BAGIMSIZDIR, ki iki yerde ayni hesabi tekrarlamak zorunda
   kalmayalim. */
static void gcl_frustum_from_camera(const float pos[3], const float target[3],
                                    const float up[3], float fovy_deg,
                                    float aspect, GclFrustum *f) {
    float fwd[3], rgt[3], u[3], tmp[3];
    float t, s;
    int i;

    if (!f) return;
    memset(f, 0, sizeof(*f));

    gclc_sub3(target, pos, fwd);
    if (gclc_norm3(fwd) < 1.0e-6f) return;      /* dejenere kamera: eleme yok */

    /* up diklestir, sonra rgt = fwd x up. */
    u[0] = up[0]; u[1] = up[1]; u[2] = up[2];
    {
        float k = gclc_dot3(u, fwd);
        u[0] -= fwd[0] * k; u[1] -= fwd[1] * k; u[2] -= fwd[2] * k;
        if (gclc_norm3(u) < 1.0e-6f) { u[0] = 0.0f; u[1] = 1.0f; u[2] = 0.0f; }
    }
    gclc_cross3(fwd, u, rgt);
    gclc_norm3(rgt);

    if (aspect <= 0.0f) aspect = 1.0f;
    t = tanf(fovy_deg * 0.5f * 0.017453292519943295f);
    s = t * aspect;

    f->pos[0] = pos[0]; f->pos[1] = pos[1]; f->pos[2] = pos[2];
    for (i = 0; i < 3; i++) { f->fwd[i] = fwd[i]; f->rgt[i] = rgt[i]; f->upv[i] = u[i]; }
    f->tan_x = s; f->tan_y = t;

    /* near: n = fwd,           d = fwd . pos + near
       far : n = -fwd,          d = -(fwd . pos + far) */
    for (i = 0; i < 3; i++) f->p[0].n[i] = fwd[i];
    f->p[0].d = gclc_dot3(fwd, pos) + GCL_NEAR_PLANE;
    for (i = 0; i < 3; i++) f->p[1].n[i] = -fwd[i];
    f->p[1].d = -(gclc_dot3(fwd, pos) + GCL_FAR_PLANE);

    /* right: ic bolge  s*(fwd.v) - rgt.v >= 0  -> n = normalize(s*fwd - rgt) */
    tmp[0] = s * fwd[0] - rgt[0];
    tmp[1] = s * fwd[1] - rgt[1];
    tmp[2] = s * fwd[2] - rgt[2];
    gclc_norm3(tmp);
    for (i = 0; i < 3; i++) f->p[2].n[i] = tmp[i];
    f->p[2].d = gclc_dot3(tmp, pos);

    /* left: n = normalize(s*fwd + rgt) */
    tmp[0] = s * fwd[0] + rgt[0];
    tmp[1] = s * fwd[1] + rgt[1];
    tmp[2] = s * fwd[2] + rgt[2];
    gclc_norm3(tmp);
    for (i = 0; i < 3; i++) f->p[3].n[i] = tmp[i];
    f->p[3].d = gclc_dot3(tmp, pos);

    /* top: n = normalize(t*fwd - upv) */
    tmp[0] = t * fwd[0] - u[0];
    tmp[1] = t * fwd[1] - u[1];
    tmp[2] = t * fwd[2] - u[2];
    gclc_norm3(tmp);
    for (i = 0; i < 3; i++) f->p[4].n[i] = tmp[i];
    f->p[4].d = gclc_dot3(tmp, pos);

    /* bottom: n = normalize(t*fwd + upv) */
    tmp[0] = t * fwd[0] + u[0];
    tmp[1] = t * fwd[1] + u[1];
    tmp[2] = t * fwd[2] + u[2];
    gclc_norm3(tmp);
    for (i = 0; i < 3; i++) f->p[5].n[i] = tmp[i];
    f->p[5].d = gclc_dot3(tmp, pos);

    f->valid = 1;
}

/* AABB, frustumun ICINDE mi (ya da kesisiyor mu)? Kutunun disinda kaldigi TEK
   bir duzlem varsa 0 doner. "Pozitif kose" testi: her duzlem icin kutunun
   normal yonunde EN ILERI kosesi alinir; o kose bile duzlemin gerisindeyse
   kutu tamamen disaridadir. */
static int gcl_aabb_in_frustum(const GclFrustum *f,
                               const float lo[3], const float hi[3]) {
    int i;
    if (!f || !f->valid) return 1;              /* frustum yok: her sey gorunur */
    for (i = 0; i < 6; i++) {
        const float *n = f->p[i].n;
        float px = (n[0] >= 0.0f) ? hi[0] : lo[0];
        float py = (n[1] >= 0.0f) ? hi[1] : lo[1];
        float pz = (n[2] >= 0.0f) ? hi[2] : lo[2];
        float dist = n[0] * px + n[1] * py + n[2] * pz - f->p[i].d;
        if (dist < 0.0f) return 0;              /* tamamen disarida */
    }
    return 1;
}

/* ---------- YAZILIM OCCLUSION TAMPONU ---------- */

typedef struct {
    float z[GCL_OCC_H * GCL_OCC_W];
    int   ready;
} GclOccluder;

static void gcl_occ_clear(GclOccluder *o) {
    int i;
    if (!o) return;
    for (i = 0; i < GCL_OCC_H * GCL_OCC_W; i++) o->z[i] = GCL_OCC_EMPTY;
    o->ready = 0;
}

/* Dunya noktasi -> (ekran x, y, ileri derinlik). Donus: 0 = nokta near
   duzleminin ARKASINDA (kullanilamaz), 1 = gecerli. */
static int gcl_occ_project(const GclFrustum *f, const float p[3],
                           float *sx, float *sy, float *depth) {
    float v[3], z, xc, yc;
    gclc_sub3(p, f->pos, v);
    z = gclc_dot3(f->fwd, v);
    if (z < GCL_NEAR_PLANE) return 0;
    xc = gclc_dot3(f->rgt, v);
    yc = gclc_dot3(f->upv, v);
    *sx = (xc / (z * f->tan_x) * 0.5f + 0.5f) * (float)GCL_OCC_W;
    *sy = (0.5f - yc / (z * f->tan_y) * 0.5f) * (float)GCL_OCC_H;
    *depth = z;
    return 1;
}

static void gcl_occ_put(GclOccluder *o, int x, int y, float z) {
    if (x < 0 || x >= GCL_OCC_W || y < 0 || y >= GCL_OCC_H) return;
    if (z < o->z[y * GCL_OCC_W + x]) o->z[y * GCL_OCC_W + x] = z;
}

/* Bir dunya-uzayi ucgenini tampona rastrele (min-derinlik). Herhangi bir
   kosesi near duzleminin arkasindaysa ucgen ATLANIR — kirpma kodu yok; bu
   yalnizca o ucgenin occluder OLMAYACAGI anlamina gelir (konservatif). */
static void gcl_occ_triangle(GclOccluder *o, const GclFrustum *f,
                             const float a[3], const float b[3], const float c[3]) {
    float ax, ay, az, bx, by, bz, cx, cy, cz;
    float minx, maxx, miny, maxy;
    float area;
    int x0, x1, y0, y1, x, y;

    if (!gcl_occ_project(f, a, &ax, &ay, &az)) return;
    if (!gcl_occ_project(f, b, &bx, &by, &bz)) return;
    if (!gcl_occ_project(f, c, &cx, &cy, &cz)) return;

    area = (bx - ax) * (cy - ay) - (by - ay) * (cx - ax);
    if (area >= -1.0e-6f && area <= 1.0e-6f) return;   /* dejenere: atla */

    minx = ax; if (bx < minx) minx = bx; if (cx < minx) minx = cx;
    maxx = ax; if (bx > maxx) maxx = bx; if (cx > maxx) maxx = cx;
    miny = ay; if (by < miny) miny = by; if (cy < miny) miny = cy;
    maxy = ay; if (by > maxy) maxy = by; if (cy > maxy) maxy = cy;

    if (maxx < 0.0f || minx >= (float)GCL_OCC_W) return;
    if (maxy < 0.0f || miny >= (float)GCL_OCC_H) return;

    x0 = (int)floorf(minx); x1 = (int)ceilf(maxx);
    y0 = (int)floorf(miny); y1 = (int)ceilf(maxy);
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > GCL_OCC_W - 1) x1 = GCL_OCC_W - 1;
    if (y1 > GCL_OCC_H - 1) y1 = GCL_OCC_H - 1;

    for (y = y0; y <= y1; y++) {
        float py = (float)y + 0.5f;
        for (x = x0; x <= x1; x++) {
            float px = (float)x + 0.5f;
            float w0 = (bx - ax) * (py - ay) - (by - ay) * (px - ax);
            float w1 = (cx - bx) * (py - by) - (cy - by) * (px - bx);
            float w2 = (ax - cx) * (py - cy) - (ay - cy) * (px - cx);
            if (area > 0.0f) {
                if (w0 < 0.0f || w1 < 0.0f || w2 < 0.0f) continue;
            } else {
                if (w0 > 0.0f || w1 > 0.0f || w2 > 0.0f) continue;
            }
            {
                float l0 = w1 / area;
                float l1 = w2 / area;
                float l2 = w0 / area;
                float z = l0 * az + l1 * bz + l2 * cz;
                gcl_occ_put(o, x, y, z);
            }
        }
    }
    o->ready = 1;
}

/* Bir AABB'nin TAMAMI tampondaki yuzeyin arkasinda mi? KONSERVATIF: yalnizca
   kutunun butun koseleri near onunde, izdusum ekranda alan kapliyor ve o
   alandaki HER hucre dolu ve kutunun EN YAKIN kosesinden daha yakinsa 1. */
static int gcl_aabb_occluded(const GclOccluder *o, const GclFrustum *f,
                             const float lo[3], const float hi[3]) {
    float nearest = GCL_OCC_EMPTY;
    float minx = GCL_OCC_EMPTY, maxx = -GCL_OCC_EMPTY;
    float miny = GCL_OCC_EMPTY, maxy = -GCL_OCC_EMPTY;
    int i, x0, x1, y0, y1, x, y;

    if (!o || !o->ready || !f || !f->valid) return 0;

    for (i = 0; i < 8; i++) {
        float p[3];
        float sx, sy, z;
        p[0] = (i & 1) ? hi[0] : lo[0];
        p[1] = (i & 2) ? hi[1] : lo[1];
        p[2] = (i & 4) ? hi[2] : lo[2];
        if (!gcl_occ_project(f, p, &sx, &sy, &z)) return 0;   /* near arkasinda: ciz */
        if (z < nearest) nearest = z;
        if (sx < minx) minx = sx;
        if (sx > maxx) maxx = sx;
        if (sy < miny) miny = sy;
        if (sy > maxy) maxy = sy;
    }

    if (maxx < 0.0f || minx >= (float)GCL_OCC_W) return 0;
    if (maxy < 0.0f || miny >= (float)GCL_OCC_H) return 0;

    x0 = (int)floorf(minx); x1 = (int)ceilf(maxx);
    y0 = (int)floorf(miny); y1 = (int)ceilf(maxy);
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > GCL_OCC_W - 1) x1 = GCL_OCC_W - 1;
    if (y1 > GCL_OCC_H - 1) y1 = GCL_OCC_H - 1;

    for (y = y0; y <= y1; y++) {
        for (x = x0; x <= x1; x++) {
            float oz = o->z[y * GCL_OCC_W + x];
            if (oz > nearest - GCL_OCC_BIAS) return 0;   /* bos ya da obje onde */
        }
    }
    return 1;
}

/* ---------- ortam degiskeni anahtarlari ---------- */

static int gcl_cull_frustum_enabled(void) {
    static int cached = -1;
    if (cached < 0) {
        const char *v = getenv("GCL_NO_FRUSTUM");
        cached = (v && v[0] && v[0] != '0') ? 0 : 1;
    }
    return cached;
}

static int gcl_cull_occlusion_enabled(void) {
    static int cached = -1;
    if (cached < 0) {
        const char *v = getenv("GCL_NO_OCCLUSION");
        cached = (v && v[0] && v[0] != '0') ? 0 : 1;
    }
    return cached;
}

#endif /* GCL_CULL_H */
