#ifndef ADVERB_H
#define ADVERB_H

#include "skrawl.h"

extern K (*adverb_table[])(K,K*,i64);

// adverbs
K applyleft(K,K*,i64);
K each(K,K*,i64);
K eachLeft(K,K*,i64);
K eachRight(K,K*,i64);
K over(K,K*,i64);
K scan(K,K*,i64);

// utility
i64 iterCount(K*,i64);

#endif
