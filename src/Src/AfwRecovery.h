#pragma once
enum class AfwFailure { None, MissingMatchingFrame, MissingDlss, Hard };
inline bool AfwFailureMayRecover(AfwFailure failure,bool dlssTransient) {
    return failure==AfwFailure::MissingMatchingFrame || (failure==AfwFailure::MissingDlss && dlssTransient);
}

// Present-lane recovery policy. Never retries hard backend/validation failures.
// A renderer command cancels it; successful accepted frames replenish the budget.
struct AfwRecovery {
    bool pending=false;int quality=0;unsigned attempts=0,stable=0;
    double due=0;
    void Cancel(){pending=false;attempts=stable=0;due=0;}
    bool Schedule(double now,int selectedQuality,bool transient){
        pending=false;stable=0;
        if(!transient||selectedQuality<0||selectedQuality>4||attempts>=3)return false;
        quality=selectedQuality;
        const double delays[]={2,5,15};due=now+delays[attempts];pending=true;return true;
    }
    bool Take(double now,bool gameplayReady){
        if(!pending||!gameplayReady||now<due)return false;
        pending=false;++attempts;return true;
    }
    void Accepted(){if(++stable>=120){attempts=0;stable=120;}}
};
