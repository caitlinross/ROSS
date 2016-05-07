
/* Copied and modified from http://pine.cs.yale.edu/pinewiki/C/AvlTree google cache */

/* implementation of an AVL tree with explicit heights */
/****
 * This is just to be able to run without ross
 ****/
#include <math.h>
#include <ross.h>

#define NUM_INTERVAL_STATS 11
/***
 * end
 *****/

// TODO set up enum for accessing correct location in array
struct stats_bin {
/*    tw_stat forward_events;
    tw_stat reverse_events;
    tw_stat num_gvts;
    tw_stat all_reduce_calls;
    tw_stat events_aborted;
    tw_stat pe_event_ties;

    tw_stat nsend_network;
    tw_stat nread_network;

    tw_stat events_rbs;

    tw_stat rb_primary;
    tw_stat rb_secondary;
    tw_stat fc_attempts;
    */
    tw_stat stats[NUM_INTERVAL_STATS];
};

struct stat_node {
  struct stat_node *child[2];    /* left and right */
  long key;                    /* beginning time for this bin */
  int height;
  stats_bin bin;
};

/* empty stat tree is just a null pointer */

#define AVL_EMPTY (0)

stat_node *find_stat_max(stat_node *t);
stat_node *init_stat_tree(tw_stat start);

stat_node *stat_increment(stat_node *t, long time_stamp, int stat_type, stat_node *root);

stat_node *add_nodes(stat_node *root);

/* return the height of a tree */
int statGetHeight(stat_node *t);

/* run sanity checks on tree (for debugging) */
/* assert will fail if heights are wrong */
void statSanityCheck(stat_node *t);

stat_node *gvt_write_bins(FILE *log, stat_node *t, tw_stime gvt);

/* free a tree */
void statDestroy(stat_node *t);

void stat_free(stat_node *t);

/* print all keys of the tree in order */
void statPrintKeys(stat_node *t);

/* delete and return minimum value in a tree */
stat_node *statDeleteMin(stat_node *t, stat_node *parent);
stat_node *statDeleteMax(stat_node *t, stat_node *parent);

// Should only need to delete at GVT
// could be 1 bin or many, so write necessary bins to file, delete those bins,
// then rebalance the tree
stat_node *statDelete(stat_node *t, long key, stat_node *parent, stat_node *root);
