#pragma once

// Hosts with a command line forward it so gtest's own flags (--gtest_filter, --gtest_repeat, ...)
// reach the runner; the app-style hosts (iOS, Android) use the argument-less form.
int RunTests(int argc, char** argv);
int RunTests();