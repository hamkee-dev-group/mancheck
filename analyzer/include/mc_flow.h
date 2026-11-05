#ifndef MC_FLOW_H
#define MC_FLOW_H
#include "mc_ts.h"

typedef struct {
    const char *file;
    unsigned line, col;
    char symbol[64];
    const char *category, *db_kind;
    char message[256];
    int use_issue_format;
} mc_finding;
typedef void (*mc_finding_sink)(const mc_finding *, void *);
typedef struct mc_flow mc_flow;
mc_flow *mc_flow_analyze(const mc_ts_file *file);
void mc_flow_destroy(mc_flow *flow);
bool mc_flow_ok(const mc_flow *flow);
mc_call_status mc_flow_status(const mc_flow *flow, TSNode call, mc_call_status fallback);
void mc_flow_findings(const mc_flow *flow, mc_finding_sink sink, void *data);
#endif
