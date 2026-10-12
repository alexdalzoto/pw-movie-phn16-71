/* dts_vol: speaker volume after the AGC with the Windows slider taper. Mono: one instance per channel,
 * last in the chain. The filter-chain maps the sink volume (slider, media keys) onto "Volume" with
 * capture.volumes scale = cubic, so the port receives the slider position s in 0..1 rather than s^3.
 * Gain = s^Exponent. The Realtek endpoint on Windows measured 20*1.7*log10(s) within about 0.5 dB
 * from 10% to 100% (20% = -23.7 dB, 50% = -10.3 dB), against s^3 for a plain PipeWire sink
 * (20% = -41.9 dB). The gain is smoothed (20 ms) so slider moves do not click. Build: see Makefile. */
#include <math.h>
#include <stdlib.h>
#include <ladspa.h>

enum { P_IN, P_OUT, P_VOL, P_EXP, P_N };

typedef struct {
    LADSPA_Data *p[P_N];
    double k;      /* one-pole smoothing coefficient */
    double g;      /* current (smoothed) linear gain */
} Vol;

static double target(Vol *s) {
    double v = *s->p[P_VOL];
    if (!(v > 0)) return 0;   /* also catches NaN */
    if (v > 1) v = 1;
    return pow(v, *s->p[P_EXP]);
}

static LADSPA_Handle instantiate(const LADSPA_Descriptor *d, unsigned long fs) {
    (void)d;
    Vol *s = calloc(1, sizeof(Vol));
    if (s) s->k = 1.0 - exp(-1.0 / (0.020 * (double)fs));
    return s;
}
static void activate(LADSPA_Handle h) { Vol *s = h; s->g = target(s); }
static void connect(LADSPA_Handle h, unsigned long port, LADSPA_Data *d) { ((Vol *)h)->p[port] = d; }
static void run(LADSPA_Handle h, unsigned long n) {
    Vol *s = h;
    const LADSPA_Data *in = s->p[P_IN];
    LADSPA_Data *out = s->p[P_OUT];
    double t = target(s), g = s->g, k = s->k;
    for (unsigned long i = 0; i < n; i++) {
        g += k * (t - g);
        out[i] = (LADSPA_Data)(in[i] * g);
    }
    s->g = fabs(g - t) < 1e-9 ? t : g;
}
static void cleanup(LADSPA_Handle h) { free(h); }

static const struct { const char *n; LADSPA_PortDescriptor d; float lo, hi; } ports[P_N] = {
    {"In", LADSPA_PORT_INPUT | LADSPA_PORT_AUDIO, 0, 0},
    {"Out", LADSPA_PORT_OUTPUT | LADSPA_PORT_AUDIO, 0, 0},
    {"Volume", LADSPA_PORT_INPUT | LADSPA_PORT_CONTROL, 0, 1},
    {"Exponent", LADSPA_PORT_INPUT | LADSPA_PORT_CONTROL, 0.5, 4},
};

static LADSPA_Descriptor desc;
static char *names[P_N]; static LADSPA_PortDescriptor pd[P_N]; static LADSPA_PortRangeHint ph[P_N];

__attribute__((constructor)) static void init(void) {
    for (int i = 0; i < P_N; i++) {
        names[i] = (char *)ports[i].n; pd[i] = ports[i].d;
        ph[i].HintDescriptor = i >= P_VOL ? LADSPA_HINT_BOUNDED_BELOW | LADSPA_HINT_BOUNDED_ABOVE : 0;
        ph[i].LowerBound = ports[i].lo; ph[i].UpperBound = ports[i].hi;
    }
    ph[P_VOL].HintDescriptor |= LADSPA_HINT_DEFAULT_MAXIMUM;
    desc.UniqueID = 0x44545356; desc.Label = "dts_vol"; desc.Properties = LADSPA_PROPERTY_HARD_RT_CAPABLE;
    desc.Name = "DTS speaker volume (Windows slider taper)"; desc.Maker = "driveraudio"; desc.Copyright = "none";
    desc.PortCount = P_N; desc.PortDescriptors = pd; desc.PortNames = (const char *const *)names; desc.PortRangeHints = ph;
    desc.instantiate = instantiate; desc.connect_port = connect; desc.activate = activate; desc.run = run; desc.cleanup = cleanup;
}
const LADSPA_Descriptor *ladspa_descriptor(unsigned long i) { return i == 0 ? &desc : NULL; }
