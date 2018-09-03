// Copyright (c) 2018 The Open-Transactions developers
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.

#ifndef TESTS_OTTESTENVIRONMENT_HPP_
#define TESTS_OTTESTENVIRONMENT_HPP_

#include <gtest/gtest.h>

class OTTestEnvironment : public testing::Environment
{
public:
    virtual ~OTTestEnvironment();

    virtual void SetUp();

    virtual void TearDown();
};

#endif /* TESTS_OTTESTENVIRONMENT_HPP_ */
