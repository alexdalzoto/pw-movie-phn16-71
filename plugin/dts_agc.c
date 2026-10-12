/* dts_agc: empirical model of the DTS Eagle "Movie" loudness maximizer (MBHL Boost 99), fitted to
 * Windows loopback recordings (groundtruth/). Stereo-linked level-dependent AGC + lookahead limiter.
 * Mirror of tools/gt/agc_model.py. Build: see Makefile. */
#include <ladspa.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

#define BLK 64
#define LOOK 150
#define W (LOOK + 1)

enum { P_INL, P_INR, P_OUTL, P_OUTR, P_GINIT, P_GMAX, P_TOUT, P_K0, P_SLOPE, P_LREF, P_TAUDET, P_TAUFALL,
       P_LGATE, P_KDOWN, P_CEIL, P_WLOW, P_WFC, P_UPCAP, P_N };

typedef struct {
    LADSPA_Data *p[P_N];
    double fs, ms, g_db, g_prev_lin, g_lin, blk_e;
    int blk_n, pos;
    float ring_l[LOOK], ring_r[LOOK];   /* delayed (gained) signal */
    float m_ring[W];                /* window-min history for the boxcar */
    double m_sum;
    long dq_idx[W + 2]; float dq_val[W + 2]; long dq_h, dq_t; long n;   /* monotonic deque */
    /* detector weighting: x + (g-1)*LP2(x), Butterworth LP2 at P_WFC (bass counts g = P_WLOW dB) */
    double wb0, wb1, wb2, wa1, wa2, wz[2][2]; float w_fc;
} Agc;

static void design_w(Agc *a) {
    a->w_fc = *a->p[P_WFC];
    double k = tan(M_PI * a->w_fc / a->fs), q = M_SQRT1_2, n = 1.0 / (1.0 + k / q + k * k);
    a->wb0 = k * k * n; a->wb1 = 2 * a->wb0; a->wb2 = a->wb0;
    a->wa1 = 2 * (k * k - 1) * n; a->wa2 = (1 - k / q + k * k) * n;
}

static double wlp(Agc *a, int c, double x) {   /* TDF-II */
    double y = a->wb0 * x + a->wz[c][0];
    a->wz[c][0] = a->wb1 * x - a->wa1 * y + a->wz[c][1];
    a->wz[c][1] = a->wb2 * x - a->wa2 * y;
    return y;
}

static LADSPA_Handle inst(const LADSPA_Descriptor *d, unsigned long fs) {
    Agc *a = calloc(1, sizeof *a); a->fs = fs; return a;
}

static void reset(Agc *a) {
    a->ms = 1e-12; a->g_db = *a->p[P_GINIT];
    a->g_prev_lin = a->g_lin = pow(10.0, a->g_db / 20.0);
    a->blk_e = 0; a->blk_n = 0; a->pos = 0; a->n = 0; a->dq_h = a->dq_t = 0; a->m_sum = W;
    for (int i = 0; i < LOOK; i++) a->ring_l[i] = a->ring_r[i] = 0;
    for (int i = 0; i < W; i++) a->m_ring[i] = 1.0f;
    a->wz[0][0] = a->wz[0][1] = a->wz[1][0] = a->wz[1][1] = 0;
    design_w(a);
}

static void activate(LADSPA_Handle h) { reset((Agc *)h); }
static void connect(LADSPA_Handle h, unsigned long port, LADSPA_Data *d) { ((Agc *)h)->p[port] = d; }

static void run(LADSPA_Handle h, unsigned long n) {
    Agc *a = h;
    const float *il = a->p[P_INL], *ir = a->p[P_INR]; float *ol = a->p[P_OUTL], *or_ = a->p[P_OUTR];
    const double fs = a->fs, B = BLK;
    const double ceil_ = *a->p[P_CEIL];
    const double a_det = 1 - exp(-B / fs / *a->p[P_TAUDET]), a_fall = 1 - exp(-B / fs / *a->p[P_TAUFALL]);
    if (*a->p[P_WFC] != a->w_fc) design_w(a);
    const double wg = pow(10.0, *a->p[P_WLOW] / 20.0) - 1.0;
    for (unsigned long i = 0; i < n; i++) {
        double t = (double)a->blk_n / BLK;
        double gl = a->g_prev_lin + (a->g_lin - a->g_prev_lin) * t;
        float yl = il[i] * gl, yr = ir[i] * gl;
        double wl = il[i] + wg * wlp(a, 0, il[i]), wr = ir[i] + wg * wlp(a, 1, ir[i]);
        a->blk_e += wl * wl + wr * wr;
        if (++a->blk_n == BLK) {                 /* block update */
            double e = a->blk_e / (2.0 * BLK); a->blk_e = 0; a->blk_n = 0;
            a->ms += (e > a->ms ? a_det : a_fall) * (e - a->ms);
            double L = 10 * log10(a->ms + 1e-20);
            if (L > *a->p[P_LGATE]) {
                double gt = fmin(*a->p[P_GMAX], *a->p[P_TOUT] - L);
                double k = gt > a->g_db ? *a->p[P_K0] * exp(*a->p[P_SLOPE] * (L - *a->p[P_LREF])) : *a->p[P_KDOWN];
                if (k > 200) k = 200;
                double step = (gt - a->g_db) * (1 - exp(-k * B / fs)), cap = *a->p[P_UPCAP] * B / fs;
                a->g_db += step > cap ? cap : step;   /* rise speed limit (dB/s); falls are not capped */
            }
            a->g_prev_lin = a->g_lin; a->g_lin = pow(10.0, a->g_db / 20.0);
        }
        /* lookahead limiter: m[n] = min need[n-LOOK..n]; g[n] = mean m[n-LOOK..n]; out[n] = y[n-LOOK]*g[n] */
        float pk = fmaxf(fabsf(yl), fabsf(yr));
        float need = pk > 1e-9f ? fminf(1.0f, (float)ceil_ / pk) : 1.0f;
        while (a->dq_t > a->dq_h && a->dq_val[(a->dq_t - 1) % (W + 2)] >= need) a->dq_t--;
        a->dq_idx[a->dq_t % (W + 2)] = a->n; a->dq_val[a->dq_t % (W + 2)] = need; a->dq_t++;
        while (a->dq_idx[a->dq_h % (W + 2)] < a->n - LOOK) a->dq_h++;
        float m = a->dq_val[a->dq_h % (W + 2)];
        a->m_sum += m - a->m_ring[a->pos]; a->m_ring[a->pos] = m;
        float gmean = (float)(a->m_sum / W);
        int dp = (int)(a->n % LOOK);
        float dl = a->ring_l[dp], dr = a->ring_r[dp];   /* y[n-LOOK] */
        a->ring_l[dp] = yl; a->ring_r[dp] = yr;
        a->pos = (a->pos + 1) % W; a->n++;
        float xl = dl * gmean, xr = dr * gmean;
        ol[i] = fmaxf(-1.0f, fminf(1.0f, xl)); or_[i] = fmaxf(-1.0f, fminf(1.0f, xr));
    }
}

static void cleanup(LADSPA_Handle h) { free(h); }

#define CTL(name, lo, hi, def) { name, LADSPA_PORT_INPUT | LADSPA_PORT_CONTROL, \
    LADSPA_HINT_BOUNDED_BELOW | LADSPA_HINT_BOUNDED_ABOVE | LADSPA_HINT_DEFAULT_MIDDLE, lo, hi, def }
static const struct { const char *n; LADSPA_PortDescriptor d; LADSPA_PortRangeHintDescriptor h; float lo, hi, def; } ports[P_N] = {
    {"In L", LADSPA_PORT_INPUT | LADSPA_PORT_AUDIO, 0, 0, 0, 0},
    {"In R", LADSPA_PORT_INPUT | LADSPA_PORT_AUDIO, 0, 0, 0, 0},
    {"Out L", LADSPA_PORT_OUTPUT | LADSPA_PORT_AUDIO, 0, 0, 0, 0},
    {"Out R", LADSPA_PORT_OUTPUT | LADSPA_PORT_AUDIO, 0, 0, 0, 0},
    CTL("Initial gain (dB)", 0, 40, 23), CTL("Max gain (dB)", 0, 60, 35), CTL("Target output RMS (dB)", -30, 0, -10.4),
    CTL("Rise rate k0 (1/s)", 0.001, 50, 0.25), CTL("Rise slope (1/dB)", 0, 1, 0.196), CTL("Rise ref level (dB)", -90, 0, -55.6),
    CTL("Detector rise tau (s)", 0.005, 5, 0.4), CTL("Detector fall tau (s)", 0.005, 5, 0.05),
    CTL("Gate level (dB)", -100, 0, -63), CTL("Fall rate (1/s)", 0.1, 100, 8), CTL("Ceiling (lin)", 0.1, 1, 0.987),
    CTL("Bass weight (dB)", -24, 6, 0), CTL("Bass split (Hz)", 50, 2000, 500), CTL("Max rise (dB/s)", 0.1, 1000, 16),
};

static LADSPA_Descriptor desc;
static char *names[P_N]; static LADSPA_PortDescriptor pd[P_N]; static LADSPA_PortRangeHint ph[P_N];

__attribute__((constructor)) static void init(void) {
    for (int i = 0; i < P_N; i++) {
        names[i] = (char *)ports[i].n; pd[i] = ports[i].d;
        ph[i].HintDescriptor = ports[i].h | (i >= P_GINIT ? LADSPA_HINT_DEFAULT_MIDDLE : 0);
        ph[i].LowerBound = ports[i].lo; ph[i].UpperBound = ports[i].hi;
        if (i >= P_GINIT) { ph[i].HintDescriptor = LADSPA_HINT_BOUNDED_BELOW | LADSPA_HINT_BOUNDED_ABOVE; }
    }
    desc.UniqueID = 0x44545341; desc.Label = "dts_agc"; desc.Properties = LADSPA_PROPERTY_HARD_RT_CAPABLE;
    desc.Name = "DTS Movie loudness AGC (empirical)"; desc.Maker = "driveraudio"; desc.Copyright = "none";
    desc.PortCount = P_N; desc.PortDescriptors = pd; desc.PortNames = (const char *const *)names; desc.PortRangeHints = ph;
    desc.instantiate = inst; desc.connect_port = connect; desc.activate = activate; desc.run = run; desc.cleanup = cleanup;
}
const LADSPA_Descriptor *ladspa_descriptor(unsigned long i) { return i == 0 ? &desc : NULL; }
