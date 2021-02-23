#!/bin/bash

set -eu
set -o pipefail

JOB_NAME=""
TARGET_PIPELINE_ID=""

COVERAGE_JOB_ID=$(curl -s \
  "${CI_API_V4_URL}/projects/${CI_PROJECT_ID}/pipelines/${CI_PIPELINE_ID}/jobs" \
  | jq --arg JOB_NAME_ARG "$COVERAGE_JOB" '.[] | select(.name==$JOB_NAME_ARG) | .id')

echo "Adding coverage. Coverage job id: '$COVERAGE_JOB_ID'"

if [ -n "$COVERAGE_JOB_ID" ]; then
  echo "Coverage was calculated by job of this pipeline."
  JOB_NAME="$COVERAGE_JOB"
  TARGET_PIPELINE_ID="$CI_PIPELINE_ID"
else
  echo "Need to get coverage data from previous pipeline."
  JOB_NAME=$CI_JOB_NAME
  TARGET_PIPELINE_ID=$(curl -s \
    "${CI_API_V4_URL}/projects/${CI_PROJECT_ID}/pipelines?ref=${TARGET_BRANCH}&status=success" \
    | jq ".[0].id")
fi

echo "Branch: $TARGET_BRANCH | Pipeline ID: $TARGET_PIPELINE_ID | Job: $JOB_NAME"

TARGET_COVERAGE=$(curl -s \
  "${CI_API_V4_URL}/projects/${CI_PROJECT_ID}/pipelines/${TARGET_PIPELINE_ID}/jobs" \
  | jq --arg JOB_NAME_ARG "$JOB_NAME" '.[] | select(.name==$JOB_NAME_ARG) | .coverage')
echo "lines: ${TARGET_COVERAGE}% (0 out of 0)"
