/* dts_bass: black-box model of the DTS Eagle TBHDX harmonic generator ("virtual bass"), fitted to
 * Windows Movie loopback recordings (groundtruth/stimuli_tbhdx). Mono: one instance per channel,
 * placed before the measured 2x2 matrix. Adds harmonics only; the fundamental stays in the matrix.
 * Mirror of tools/gt/bass_model.py (two bass sub-bands, peak-normalised Chebyshev waveshaper,
 * 3rd-order post low-pass). Build: see Makefile. Test build: cc -DBASS_TEST (raw float in/out). */
#include <math.h>
#include <stdlib.h>
#include <string.h>
#ifndef BASS_TEST
#include <ladspa.h>
#else
typedef float LADSPA_Data;
#endif

#define NK 5   /* harmonics 2..6 */

enum { P_IN, P_OUT, P_AMOUNT, P_SPLIT, P_TOP, P_POST, P_REL,
       P_L2, P_L3, P_L4, P_L5, P_L6, P_H2, P_H3, P_H4, P_H5, P_H6, P_N };

typedef struct { double b0, b1, b2, a1, a2, z1, z2; } Bq;   /* TDF-II, y = b.x - a.y */

typedef struct {
    LADSPA_Data *p[P_N];
    double fs;
    Bq lo, hp, lp, p1, p2;   /* low band LP2; high band HP2+LP2; post = first order (p1) + Q=1 biquad (p2) */
    double env_lo, env_hi;
    float split, top, post;  /* designed-for values, to redesign when controls change */
} Bass;

static double bq(Bq *f, double x) {
    double y = f->b0 * x + f->z1;
    f->z1 = f->b1 * x - f->a1 * y + f->z2;
    f->z2 = f->b2 * x - f->a2 * y;
    return y;
}

/* Bilinear with prewarp, matching scipy.signal.butter section by section. */
static void lp2(Bq *f, double fc, double q, double fs) {
    double k = tan(M_PI * fc / fs), n = 1.0 / (1.0 + k / q + k * k);
    f->b0 = k * k * n; f->b1 = 2 * f->b0; f->b2 = f->b0;
    f->a1 = 2 * (k * k - 1) * n; f->a2 = (1 - k / q + k * k) * n;
}
static void hp2(Bq *f, double fc, double q, double fs) {
    double k = tan(M_PI * fc / fs), n = 1.0 / (1.0 + k / q + k * k);
    f->b0 = n; f->b1 = -2 * n; f->b2 = n;
    f->a1 = 2 * (k * k - 1) * n; f->a2 = (1 - k / q + k * k) * n;
}
static void lp1(Bq *f, double fc, double fs) {
    double k = tan(M_PI * fc / fs), n = 1.0 / (1.0 + k);
    f->b0 = k * n; f->b1 = k * n; f->b2 = 0; f->a1 = (k - 1) * n; f->a2 = 0;
}

static void design(Bass *s) {
    s->split = *s->p[P_SPLIT]; s->top = *s->p[P_TOP]; s->post = *s->p[P_POST];
    lp2(&s->lo, s->split, M_SQRT1_2, s->fs);
    hp2(&s->hp, s->split, M_SQRT1_2, s->fs);
    lp2(&s->lp, s->top, M_SQRT1_2, s->fs);
    lp1(&s->p1, s->post, s->fs);
    lp2(&s->p2, s->post, 1.0, s->fs);
}

static void *inst(double fs) { Bass *s = calloc(1, sizeof *s); s->fs = fs; return s; }

static void reset(Bass *s) {
    Bq *f[] = {&s->lo, &s->hp, &s->lp, &s->p1, &s->p2};
    for (int i = 0; i < 5; i++) f[i]->z1 = f[i]->z2 = 0;
    s->env_lo = s->env_hi = 0;
    design(s);
}

/* sum_k c_k T_k(u), k = 2..6, Chebyshev recurrence on the clipped normalised band */
static double shape(double u, const LADSPA_Data *const *c) {
    if (u > 1) u = 1; else if (u < -1) u = -1;
    double t0 = 1, t1 = u, acc = 0;
    for (int k = 2; k <= 6; k++) {
        double t2 = 2 * u * t1 - t0;
        acc += *c[k - 2] * t2;
        t0 = t1; t1 = t2;
    }
    return acc;
}

static void run_buf(Bass *s, const LADSPA_Data *in, LADSPA_Data *out, unsigned long n) {
    if (*s->p[P_SPLIT] != s->split || *s->p[P_TOP] != s->top || *s->p[P_POST] != s->post) design(s);
    const double rel = exp(-1.0 / (*s->p[P_REL] * s->fs)), amt = *s->p[P_AMOUNT];
    const LADSPA_Data *cl[NK] = {s->p[P_L2], s->p[P_L3], s->p[P_L4], s->p[P_L5], s->p[P_L6]};
    const LADSPA_Data *ch[NK] = {s->p[P_H2], s->p[P_H3], s->p[P_H4], s->p[P_H5], s->p[P_H6]};
    for (unsigned long i = 0; i < n; i++) {
        double x = in[i];
        double bl = bq(&s->lo, x), bh = bq(&s->lp, bq(&s->hp, x));
        double al = fabs(bl), ah = fabs(bh);
        s->env_lo = al > s->env_lo ? al : rel * s->env_lo + (1 - rel) * al;
        s->env_hi = ah > s->env_hi ? ah : rel * s->env_hi + (1 - rel) * ah;
        double el = fmax(s->env_lo, 1e-9), eh = fmax(s->env_hi, 1e-9);
        double h = el * shape(bl / el, cl) + eh * shape(bh / eh, ch);
        h = bq(&s->p2, bq(&s->p1, h));
        if (fabs(h) < 1e-20) h = 0;   /* keep denormals out of the filter states */
        out[i] = (LADSPA_Data)(x + amt * h);
    }
}

#ifndef BASS_TEST
static LADSPA_Handle instantiate(const LADSPA_Descriptor *d, unsigned long fs) { (void)d; return inst(fs); }
static void activate(LADSPA_Handle h) { reset((Bass *)h); }
static void connect(LADSPA_Handle h, unsigned long port, LADSPA_Data *d) { ((Bass *)h)->p[port] = d; }
static void run(LADSPA_Handle h, unsigned long n) { Bass *s = h; run_buf(s, s->p[P_IN], s->p[P_OUT], n); }
static void cleanup(LADSPA_Handle h) { free(h); }

/* Defaults = tools/gt/bass_fit.json (Windows Movie fit). */
static const struct { const char *n; LADSPA_PortDescriptor d; float lo, hi; } ports[P_N] = {
    {"In", LADSPA_PORT_INPUT | LADSPA_PORT_AUDIO, 0, 0},
    {"Out", LADSPA_PORT_OUTPUT | LADSPA_PORT_AUDIO, 0, 0},
    {"Amount", LADSPA_PORT_INPUT | LADSPA_PORT_CONTROL, 0, 4},
    {"Split (Hz)", LADSPA_PORT_INPUT | LADSPA_PORT_CONTROL, 20, 200},
    {"Band top (Hz)", LADSPA_PORT_INPUT | LADSPA_PORT_CONTROL, 100, 2000},
    {"Post low-pass (Hz)", LADSPA_PORT_INPUT | LADSPA_PORT_CONTROL, 100, 4000},
    {"Release (s)", LADSPA_PORT_INPUT | LADSPA_PORT_CONTROL, 0.005, 2},
    {"Low c2", LADSPA_PORT_INPUT | LADSPA_PORT_CONTROL, 0, 5}, {"Low c3", LADSPA_PORT_INPUT | LADSPA_PORT_CONTROL, 0, 5},
    {"Low c4", LADSPA_PORT_INPUT | LADSPA_PORT_CONTROL, 0, 5}, {"Low c5", LADSPA_PORT_INPUT | LADSPA_PORT_CONTROL, 0, 5},
    {"Low c6", LADSPA_PORT_INPUT | LADSPA_PORT_CONTROL, 0, 5},
    {"High c2", LADSPA_PORT_INPUT | LADSPA_PORT_CONTROL, 0, 5}, {"High c3", LADSPA_PORT_INPUT | LADSPA_PORT_CONTROL, 0, 5},
    {"High c4", LADSPA_PORT_INPUT | LADSPA_PORT_CONTROL, 0, 5}, {"High c5", LADSPA_PORT_INPUT | LADSPA_PORT_CONTROL, 0, 5},
    {"High c6", LADSPA_PORT_INPUT | LADSPA_PORT_CONTROL, 0, 5},
};

static LADSPA_Descriptor desc;
static char *names[P_N]; static LADSPA_PortDescriptor pd[P_N]; static LADSPA_PortRangeHint ph[P_N];

__attribute__((constructor)) static void init(void) {
    for (int i = 0; i < P_N; i++) {
        names[i] = (char *)ports[i].n; pd[i] = ports[i].d;
        ph[i].HintDescriptor = i >= P_AMOUNT ? LADSPA_HINT_BOUNDED_BELOW | LADSPA_HINT_BOUNDED_ABOVE : 0;
        ph[i].LowerBound = ports[i].lo; ph[i].UpperBound = ports[i].hi;
    }
    desc.UniqueID = 0x44545342; desc.Label = "dts_bass"; desc.Properties = LADSPA_PROPERTY_HARD_RT_CAPABLE;
    desc.Name = "DTS Movie virtual bass (empirical TBHDX harmonics)"; desc.Maker = "driveraudio"; desc.Copyright = "none";
    desc.PortCount = P_N; desc.PortDescriptors = pd; desc.PortNames = (const char *const *)names; desc.PortRangeHints = ph;
    desc.instantiate = instantiate; desc.connect_port = connect; desc.activate = activate; desc.run = run; desc.cleanup = cleanup;
}
const LADSPA_Descriptor *ladspa_descriptor(unsigned long i) { return i == 0 ? &desc : NULL; }

#else
/* Test harness: dts_bass_test in.f32 out.f32 fs amount split top post rel l2..l6 h2..h6 */
#include <stdio.h>
int main(int argc, char **argv) {
    if (argc != 4 + 15) { fprintf(stderr, "usage: in out fs + 15 params\n"); return 2; }
    static float ctl[P_N];
    Bass *s = inst(atof(argv[3]));
    for (int i = P_AMOUNT; i < P_N; i++) { ctl[i] = (float)atof(argv[4 + i - P_AMOUNT]); s->p[i] = &ctl[i]; }
    reset(s);
    FILE *fi = fopen(argv[1], "rb"), *fo = fopen(argv[2], "wb");
    float buf[1024], ob[1024]; size_t n;
    while ((n = fread(buf, sizeof(float), 1024, fi)) > 0) { run_buf(s, buf, ob, n); fwrite(ob, sizeof(float), n, fo); }
    fclose(fi); fclose(fo); free(s);
    return 0;
}
#endif
