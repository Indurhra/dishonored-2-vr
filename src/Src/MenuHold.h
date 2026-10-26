#pragma once
struct MenuHold {
    double since=-1, last=-1;
    bool spent=false, previousBack=false;
    // A gap in input cancels a pending hold; holding through close never reopens.
    int Tick(double now, bool x, bool back, bool visible) {
        if(last>=0 && now-last>0.3) { since=-1; spent=x; }
        last=now;
        const bool close=visible && back && !previousBack; previousBack=back;
        if(!x) { since=-1; spent=false; }
        if(close) { spent=x; return -1; }
        if(visible) { spent=spent || x; return 0; }
        if(x && !spent) {
            if(since<0) since=now;
            if(now-since>=3.0) { spent=true; return 1; }
        }
        return 0;
    }
    void Lost() { since=-1; last=-1; spent=true; previousBack=false; }
};
