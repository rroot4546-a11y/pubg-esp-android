package com.oll.esp;

final class Target {
    final float l, t, r, b, hx, hy;
    final boolean enemy;
    final int dist;
    final float[] lines;

    Target(float l, float t, float r, float b, boolean enemy, int dist,
           float hx, float hy, float[] lines) {
        this.l = l; this.t = t; this.r = r; this.b = b;
        this.enemy = enemy; this.dist = dist;
        this.hx = hx; this.hy = hy; this.lines = lines;
    }
}
