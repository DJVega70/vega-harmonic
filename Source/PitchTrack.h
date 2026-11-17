#pragma once
#include <cmath>
#include <vector>
#include <algorithm>

/** Small YIN pitch tracker working on a 4x-decimated mono signal (30 Hz .. ~1.2 kHz). */
class PitchTrack
{
public:
    void prepare (double sampleRate)
    {
        fs = sampleRate / dec;
        bufN = 1024; buf.assign ((size_t) bufN * 2, 0.0f); w = 0; cnt = 0; acc = 0; lp1 = lp2 = 0;
        maxTau = std::min (bufN / 2 - 2, (int) (fs / 30.0)); minTau = std::max (2, (int) (fs / 1200.0));
        d.assign ((size_t) maxTau + 2, 0.0f);
        f0 = 0; confidence = 0;
    }
    /** feed one sample; returns true when a new estimate was made */
    bool push (float x)
    {
        // simple 2-pole low-pass before decimation
        const float a = 0.35f;
        lp1 += a * (x - lp1); lp2 += a * (lp1 - lp2);
        if (++cnt < dec) return false;
        cnt = 0;
        buf[(size_t) w] = lp2; buf[(size_t) (w + bufN)] = lp2;
        w = (w + 1) % bufN;
        if (++acc < hop) return false;
        acc = 0; analyse(); return true;
    }
    float f0 = 0, confidence = 0;   // Hz (0 = unvoiced), 0..1
private:
    void analyse()
    {
        const float* s = buf.data() + w;          // oldest .. newest, contiguous thanks to the double buffer
        const int W = bufN - maxTau;
        double energy = 0; for (int i = 0; i < bufN; ++i) energy += s[i] * s[i];
        if (energy < 1e-7 * bufN) { f0 = 0; confidence = 0; return; }
        d[0] = 1; double run = 0;
        for (int tau = 1; tau <= maxTau; ++tau)
        {
            double sum = 0;
            for (int i = 0; i < W; ++i) { const float df = s[i] - s[i + tau]; sum += df * df; }
            run += sum;
            d[(size_t) tau] = run > 0 ? (float) (sum * tau / run) : 1.0f;
        }
        int best = -1;
        for (int tau = minTau; tau < maxTau; ++tau)
            if (d[(size_t) tau] < 0.15f) { while (tau + 1 < maxTau && d[(size_t) tau + 1] < d[(size_t) tau]) ++tau; best = tau; break; }
        if (best < 0) { float m = 1; for (int tau = minTau; tau < maxTau; ++tau) if (d[(size_t) tau] < m) { m = d[(size_t) tau]; best = tau; } if (m > 0.35f) { f0 = 0; confidence = 0; return; } }
        // parabolic refinement
        double t = best;
        if (best > 1 && best < maxTau)
        {
            const double a = d[(size_t) best - 1], b = d[(size_t) best], c = d[(size_t) best + 1], den = a - 2 * b + c;
            if (std::abs (den) > 1e-9) t = best + 0.5 * (a - c) / den;
        }
        f0 = (float) (fs / t); confidence = 1.0f - std::min (1.0f, d[(size_t) best]);
    }
    static constexpr int dec = 4, hop = 128;
    double fs = 12000; int bufN = 1024, w = 0, cnt = 0, acc = 0, maxTau = 400, minTau = 10;
    std::vector<float> buf, d; float lp1 = 0, lp2 = 0;
};
