//***************************************************************************
//* Copyright (c) 2011-2013 Saint-Petersburg Academic University
//* All Rights Reserved
//* See file LICENSE for details.
//****************************************************************************

#pragma once

#include "stage.hpp"

namespace debruijn_graph {

class GenomicInfoFiller : public spades::AssemblyStage {
  public:
    GenomicInfoFiller()
        : AssemblyStage("EC Threshold Finding", "ec_threshold_finder") {}

    void run(conj_graph_pack &gp);
};

}
