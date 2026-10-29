#pragma once
namespace DlssLive {
// Bound transient retries. Sustained successful evaluations replenish the budget.
struct RecoveryBudget {
    unsigned retries=0, successes=0;
    bool Retry() { successes=0; if(retries>=3)return false; ++retries; return true; }
    void Success() { if(++successes>=120){retries=0;successes=0;} }
    void Reset() { retries=successes=0; }
};
}
