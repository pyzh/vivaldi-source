// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/variations/variations_seed_simulator.h"

#include <stdint.h>

#include <map>

#include "base/macros.h"
#include "base/strings/stringprintf.h"
#include "components/variations/processed_study.h"
#include "components/variations/proto/study.pb.h"
#include "components/variations/variations_associated_data.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace variations {

namespace {

// An implementation of EntropyProvider that always returns a specific entropy
// value, regardless of field trial.
class TestEntropyProvider : public base::FieldTrial::EntropyProvider {
 public:
  explicit TestEntropyProvider(double entropy_value)
      : entropy_value_(entropy_value) {}
  ~TestEntropyProvider() override {}

  // base::FieldTrial::EntropyProvider implementation:
  double GetEntropyForTrial(const std::string& trial_name,
                            uint32_t randomization_seed) const override {
    return entropy_value_;
  }

 private:
  const double entropy_value_;

  DISALLOW_COPY_AND_ASSIGN(TestEntropyProvider);
};

// Creates and activates a single-group field trial with name |trial_name| and
// group |group_name| and variations |params| (if not NULL).
void CreateTrial(const std::string& trial_name,
                 const std::string& group_name,
                 const std::map<std::string, std::string>* params) {
  base::FieldTrialList::CreateFieldTrial(trial_name, group_name);
  if (params != NULL)
    AssociateVariationParams(trial_name, group_name, *params);
  base::FieldTrialList::FindFullName(trial_name);
}

// Creates a study with the given |study_name| and |consistency|.
Study CreateStudy(const std::string& study_name,
                  Study_Consistency consistency) {
  Study study;
  study.set_name(study_name);
  study.set_consistency(consistency);
  return study;
}

// Adds an experiment to |study| with the specified |experiment_name| and
// |probability| values and sets it as the study's default experiment.
Study_Experiment* AddExperiment(const std::string& experiment_name,
                                int probability,
                                Study* study) {
  Study_Experiment* experiment = study->add_experiment();
  experiment->set_name(experiment_name);
  experiment->set_probability_weight(probability);
  study->set_default_experiment_name(experiment_name);
  return experiment;
}

// Add an experiment param with |param_name| and |param_value| to |experiment|.
Study_Experiment_Param* AddExperimentParam(const std::string& param_name,
                                           const std::string& param_value,
                                           Study_Experiment* experiment) {
  Study_Experiment_Param* param = experiment->add_param();
  param->set_name(param_name);
  param->set_value(param_value);
  return param;
}

}  // namespace

class VariationsSeedSimulatorTest : public ::testing::Test {
 public:
  VariationsSeedSimulatorTest() : field_trial_list_(NULL) {
  }

  ~VariationsSeedSimulatorTest() override {
    // Ensure that the maps are cleared between tests, since they are stored as
    // process singletons.
    testing::ClearAllVariationIDs();
    testing::ClearAllVariationParams();
  }

  // Uses a VariationsSeedSimulator to simulate the differences between
  // |studies| and the current field trial state.
  VariationsSeedSimulator::Result SimulateDifferences(
      const std::vector<ProcessedStudy>& studies) {
    TestEntropyProvider provider(0.5);
    VariationsSeedSimulator seed_simulator(provider);
    return seed_simulator.ComputeDifferences(studies);
  }

  // Simulates the differences between |study| and the current field trial
  // state, returning a string like "1 2 3", where 1 is the number of regular
  // group changes, 2 is the number of "kill best effort" group changes and 3
  // is the number of "kill critical" group changes.
  std::string SimulateStudyDifferences(const Study* study) {
    std::vector<ProcessedStudy> studies;
    if (!ProcessedStudy::ValidateAndAppendStudy(study, false, &studies))
      return "invalid study";
    return ConvertSimulationResultToString(SimulateDifferences(studies));
  }

  // Simulates the differences between expired |study| and the current field
  // trial state, returning a string like "1 2 3", where 1 is the number of
  // regular group changes, 2 is the number of "kill best effort" group changes
  // and 3 is the number of "kill critical" group changes.
  std::string SimulateStudyDifferencesExpired(const Study* study) {
    std::vector<ProcessedStudy> studies;
    if (!ProcessedStudy::ValidateAndAppendStudy(study, true, &studies))
      return "invalid study";
    if (!studies[0].is_expired())
      return "not expired";
    return ConvertSimulationResultToString(SimulateDifferences(studies));
  }

  // Formats |result| as a string with format "1 2 3", where 1 is the number of
  // regular group changes, 2 is the number of "kill best effort" group changes
  // and 3 is the number of "kill critical" group changes.
  std::string ConvertSimulationResultToString(
      const VariationsSeedSimulator::Result& result) {
    return base::StringPrintf("%d %d %d",
                              result.normal_group_change_count,
                              result.kill_best_effort_group_change_count,
                              result.kill_critical_group_change_count);
  }

 private:
  base::FieldTrialList field_trial_list_;

  DISALLOW_COPY_AND_ASSIGN(VariationsSeedSimulatorTest);
};

TEST_F(VariationsSeedSimulatorTest, PermanentNoChanges) {
  CreateTrial("A", "B", NULL);

  std::vector<ProcessedStudy> processed_studies;
  Study study = CreateStudy("A", Study_Consistency_PERMANENT);
  Study_Experiment* experiment = AddExperiment("B", 100, &study);

  EXPECT_EQ("0 0 0", SimulateStudyDifferences(&study));

  experiment->set_type(Study_Experiment_Type_NORMAL);
  EXPECT_EQ("0 0 0", SimulateStudyDifferences(&study));
  experiment->set_type(Study_Experiment_Type_IGNORE_CHANGE);
  EXPECT_EQ("0 0 0", SimulateStudyDifferences(&study));
  experiment->set_type(Study_Experiment_Type_KILL_BEST_EFFORT);
  EXPECT_EQ("0 0 0", SimulateStudyDifferences(&study));
  experiment->set_type(Study_Experiment_Type_KILL_CRITICAL);
  EXPECT_EQ("0 0 0", SimulateStudyDifferences(&study));
}

TEST_F(VariationsSeedSimulatorTest, PermanentGroupChange) {
  CreateTrial("A", "B", NULL);

  Study study = CreateStudy("A", Study_Consistency_PERMANENT);
  Study_Experiment* experiment = AddExperiment("C", 100, &study);

  EXPECT_EQ("1 0 0", SimulateStudyDifferences(&study));

  // Changing "C" group type should not affect the type of change. (Since the
  // type is evaluated for the "old" group.)
  experiment->set_type(Study_Experiment_Type_NORMAL);
  EXPECT_EQ("1 0 0", SimulateStudyDifferences(&study));
  experiment->set_type(Study_Experiment_Type_IGNORE_CHANGE);
  EXPECT_EQ("1 0 0", SimulateStudyDifferences(&study));
  experiment->set_type(Study_Experiment_Type_KILL_BEST_EFFORT);
  EXPECT_EQ("1 0 0", SimulateStudyDifferences(&study));
  experiment->set_type(Study_Experiment_Type_KILL_CRITICAL);
  EXPECT_EQ("1 0 0", SimulateStudyDifferences(&study));
}

TEST_F(VariationsSeedSimulatorTest, PermanentExpired) {
  CreateTrial("A", "B", NULL);

  Study study = CreateStudy("A", Study_Consistency_PERMANENT);
  Study_Experiment* experiment = AddExperiment("B", 1, &study);
  AddExperiment("C", 0, &study);

  // There should be a difference because the study is expired, which should
  // result in the default group "C" being chosen.
  EXPECT_EQ("1 0 0", SimulateStudyDifferencesExpired(&study));

  experiment->set_type(Study_Experiment_Type_NORMAL);
  EXPECT_EQ("1 0 0", SimulateStudyDifferencesExpired(&study));
  experiment->set_type(Study_Experiment_Type_IGNORE_CHANGE);
  EXPECT_EQ("0 0 0", SimulateStudyDifferencesExpired(&study));
  experiment->set_type(Study_Experiment_Type_KILL_BEST_EFFORT);
  EXPECT_EQ("0 1 0", SimulateStudyDifferencesExpired(&study));
  experiment->set_type(Study_Experiment_Type_KILL_CRITICAL);
  EXPECT_EQ("0 0 1", SimulateStudyDifferencesExpired(&study));
}

TEST_F(VariationsSeedSimulatorTest, SessionRandomized) {
  CreateTrial("A", "B", NULL);

  Study study = CreateStudy("A", Study_Consistency_SESSION);
  Study_Experiment* experiment = AddExperiment("B", 1, &study);
  AddExperiment("C", 1, &study);
  AddExperiment("D", 1, &study);

  // There should be no differences, since a session randomized study can result
  // in any of the groups being chosen on startup.
  EXPECT_EQ("0 0 0", SimulateStudyDifferences(&study));

  experiment->set_type(Study_Experiment_Type_NORMAL);
  EXPECT_EQ("0 0 0", SimulateStudyDifferences(&study));
  experiment->set_type(Study_Experiment_Type_IGNORE_CHANGE);
  EXPECT_EQ("0 0 0", SimulateStudyDifferences(&study));
  experiment->set_type(Study_Experiment_Type_KILL_BEST_EFFORT);
  EXPECT_EQ("0 0 0", SimulateStudyDifferences(&study));
  experiment->set_type(Study_Experiment_Type_KILL_CRITICAL);
  EXPECT_EQ("0 0 0", SimulateStudyDifferences(&study));
}

TEST_F(VariationsSeedSimulatorTest, SessionRandomizedGroupRemoved) {
  CreateTrial("A", "B", NULL);

  Study study = CreateStudy("A", Study_Consistency_SESSION);
  AddExperiment("C", 1, &study);
  AddExperiment("D", 1, &study);

  // There should be a difference since there is no group "B" in the new config.
  EXPECT_EQ("1 0 0", SimulateStudyDifferences(&study));
}

TEST_F(VariationsSeedSimulatorTest, SessionRandomizedGroupProbabilityZero) {
  CreateTrial("A", "B", NULL);

  Study study = CreateStudy("A", Study_Consistency_SESSION);
  Study_Experiment* experiment = AddExperiment("B", 0, &study);
  AddExperiment("C", 1, &study);
  AddExperiment("D", 1, &study);

  // There should be a difference since group "B" has probability 0.
  EXPECT_EQ("1 0 0", SimulateStudyDifferences(&study));

  experiment->set_type(Study_Experiment_Type_NORMAL);
  EXPECT_EQ("1 0 0", SimulateStudyDifferences(&study));
  experiment->set_type(Study_Experiment_Type_IGNORE_CHANGE);
  EXPECT_EQ("0 0 0", SimulateStudyDifferences(&study));
  experiment->set_type(Study_Experiment_Type_KILL_BEST_EFFORT);
  EXPECT_EQ("0 1 0", SimulateStudyDifferences(&study));
  experiment->set_type(Study_Experiment_Type_KILL_CRITICAL);
  EXPECT_EQ("0 0 1", SimulateStudyDifferences(&study));
}

TEST_F(VariationsSeedSimulatorTest, SessionRandomizedExpired) {
  CreateTrial("A", "B", NULL);

  Study study = CreateStudy("A", Study_Consistency_SESSION);
  Study_Experiment* experiment = AddExperiment("B", 1, &study);
  AddExperiment("C", 1, &study);
  AddExperiment("D", 1, &study);

  // There should be a difference because the study is expired, which should
  // result in the default group "D" being chosen.
  EXPECT_EQ("1 0 0", SimulateStudyDifferencesExpired(&study));

  experiment->set_type(Study_Experiment_Type_NORMAL);
  EXPECT_EQ("1 0 0", SimulateStudyDifferencesExpired(&study));
  experiment->set_type(Study_Experiment_Type_IGNORE_CHANGE);
  EXPECT_EQ("0 0 0", SimulateStudyDifferencesExpired(&study));
  experiment->set_type(Study_Experiment_Type_KILL_BEST_EFFORT);
  EXPECT_EQ("0 1 0", SimulateStudyDifferencesExpired(&study));
  experiment->set_type(Study_Experiment_Type_KILL_CRITICAL);
  EXPECT_EQ("0 0 1", SimulateStudyDifferencesExpired(&study));
}

TEST_F(VariationsSeedSimulatorTest, ParamsUnchanged) {
  std::map<std::string, std::string> params;
  params["p1"] = "x";
  params["p2"] = "y";
  params["p3"] = "z";
  CreateTrial("A", "B", &params);

  std::vector<ProcessedStudy> processed_studies;
  Study study = CreateStudy("A", Study_Consistency_PERMANENT);
  Study_Experiment* experiment = AddExperiment("B", 100, &study);
  AddExperimentParam("p2", "y", experiment);
  AddExperimentParam("p1", "x", experiment);
  AddExperimentParam("p3", "z", experiment);

  EXPECT_EQ("0 0 0", SimulateStudyDifferences(&study));

  experiment->set_type(Study_Experiment_Type_NORMAL);
  EXPECT_EQ("0 0 0", SimulateStudyDifferences(&study));
  experiment->set_type(Study_Experiment_Type_IGNORE_CHANGE);
  EXPECT_EQ("0 0 0", SimulateStudyDifferences(&study));
  experiment->set_type(Study_Experiment_Type_KILL_BEST_EFFORT);
  EXPECT_EQ("0 0 0", SimulateStudyDifferences(&study));
  experiment->set_type(Study_Experiment_Type_KILL_CRITICAL);
  EXPECT_EQ("0 0 0", SimulateStudyDifferences(&study));
}

TEST_F(VariationsSeedSimulatorTest, ParamsChanged) {
  std::map<std::string, std::string> params;
  params["p1"] = "x";
  params["p2"] = "y";
  params["p3"] = "z";
  CreateTrial("A", "B", &params);

  std::vector<ProcessedStudy> processed_studies;
  Study study = CreateStudy("A", Study_Consistency_PERMANENT);
  Study_Experiment* experiment = AddExperiment("B", 100, &study);
  AddExperimentParam("p2", "test", experiment);
  AddExperimentParam("p1", "x", experiment);
  AddExperimentParam("p3", "z", experiment);

  // The param lists differ.
  EXPECT_EQ("1 0 0", SimulateStudyDifferences(&study));

  experiment->set_type(Study_Experiment_Type_NORMAL);
  EXPECT_EQ("1 0 0", SimulateStudyDifferences(&study));
  experiment->set_type(Study_Experiment_Type_IGNORE_CHANGE);
  EXPECT_EQ("0 0 0", SimulateStudyDifferences(&study));
  experiment->set_type(Study_Experiment_Type_KILL_BEST_EFFORT);
  EXPECT_EQ("0 1 0", SimulateStudyDifferences(&study));
  experiment->set_type(Study_Experiment_Type_KILL_CRITICAL);
  EXPECT_EQ("0 0 1", SimulateStudyDifferences(&study));
}

TEST_F(VariationsSeedSimulatorTest, ParamsRemoved) {
  std::map<std::string, std::string> params;
  params["p1"] = "x";
  params["p2"] = "y";
  params["p3"] = "z";
  CreateTrial("A", "B", &params);

  std::vector<ProcessedStudy> processed_studies;
  Study study = CreateStudy("A", Study_Consistency_PERMANENT);
  Study_Experiment* experiment = AddExperiment("B", 100, &study);

  // The current group has params, but the new config doesn't have any.
  EXPECT_EQ("1 0 0", SimulateStudyDifferences(&study));

  experiment->set_type(Study_Experiment_Type_NORMAL);
  EXPECT_EQ("1 0 0", SimulateStudyDifferences(&study));
  experiment->set_type(Study_Experiment_Type_IGNORE_CHANGE);
  EXPECT_EQ("0 0 0", SimulateStudyDifferences(&study));
  experiment->set_type(Study_Experiment_Type_KILL_BEST_EFFORT);
  EXPECT_EQ("0 1 0", SimulateStudyDifferences(&study));
  experiment->set_type(Study_Experiment_Type_KILL_CRITICAL);
  EXPECT_EQ("0 0 1", SimulateStudyDifferences(&study));
}

TEST_F(VariationsSeedSimulatorTest, ParamsAdded) {
  CreateTrial("A", "B", NULL);

  std::vector<ProcessedStudy> processed_studies;
  Study study = CreateStudy("A", Study_Consistency_PERMANENT);
  Study_Experiment* experiment = AddExperiment("B", 100, &study);
  AddExperimentParam("p2", "y", experiment);
  AddExperimentParam("p1", "x", experiment);
  AddExperimentParam("p3", "z", experiment);

  // The current group has no params, but the config has added some.
  EXPECT_EQ("1 0 0", SimulateStudyDifferences(&study));

  experiment->set_type(Study_Experiment_Type_NORMAL);
  EXPECT_EQ("1 0 0", SimulateStudyDifferences(&study));
  experiment->set_type(Study_Experiment_Type_IGNORE_CHANGE);
  EXPECT_EQ("0 0 0", SimulateStudyDifferences(&study));
  experiment->set_type(Study_Experiment_Type_KILL_BEST_EFFORT);
  EXPECT_EQ("0 1 0", SimulateStudyDifferences(&study));
  experiment->set_type(Study_Experiment_Type_KILL_CRITICAL);
  EXPECT_EQ("0 0 1", SimulateStudyDifferences(&study));
}

}  // namespace variations
