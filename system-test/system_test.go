// Copyright 2019 Google Inc. All Rights Reserved.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

package testing

import (
	"bytes"
	"flag"
	"fmt"
	"io/ioutil"
	"os"
	"os/exec"
	"runtime"
	"strings"
	"testing"
	"text/template"
	"time"

	"github.com/google/pprof/profile"
)

var (
	binaryHost          = flag.String("binary_host", "", "host from which to download precompiled binaries; if no value is specified, binaries will be built from source.")
	runOnlyV8CanaryTest = flag.Bool("run_only_v8_canary_test", false, "if true test will be run only with the v8-canary build, otherwise, no tests will be run with v8 canary build")
	pprofDir            = flag.String("pprof_nodejs_path", "", "path to directory containing pprof-nodejs module")

	runID = strings.Replace(time.Now().Format("2006-01-02-15-04-05.000000-0700"), ".", "-", -1)
)

var tmpl = template.Must(template.New("benchTemplate").Parse(`
#! /bin/bash
(

retry() {
  for i in {1..3}; do
    "${@}" && return 0
  done
  return 1
}

# Display commands being run.
set -x

# Note directory from which test is being run.
BASE_DIR=$(pwd)

# Install desired version of Node.JS.
# nvm install writes to stderr and stdout on successful install, so both are
# redirected.
. ~/.nvm/nvm.sh &>/dev/null # load nvm.
{{if .NVMMirror}}NVM_NODEJS_ORG_MIRROR={{.NVMMirror}}{{end}} retry nvm install {{.NodeVersion}} &>/dev/null

NODEDIR=$(dirname $(dirname $(which node)))

# Build and pack pprof module.
cd {{.PprofDir}}

# TODO: remove this workaround when a new version of nan (current version 
#       2.12.1) is released.
# For v8-canary tests, we need to use the version of NAN on github, which 
# contains unreleased fixes that allow the native component to be compiled
# with Node's V8 canary build.
{{if .NVMMirror}} retry npm install https://github.com/nodejs/nan.git {{end}} >/dev/null

{{if .BinaryHost}}
retry npm install --nodedir="$NODEDIR" --fallback-to-build=false --pprof_binary_host_mirror={{.BinaryHost}} >/dev/null
{{else}}
retry npm install --nodedir="$NODEDIR" >/dev/null
{{end}}

retry npm install --nodedir="$NODEDIR" >/dev/null
npm run compile
npm pack >/dev/null
VERSION=$(node -e "console.log(require('./package.json').version);")
PROFILER="{{.PprofDir}}/pprof-$VERSION.tgz"

# Create and set up directory for running benchmark.
TESTDIR="$BASE_DIR/{{.Name}}"
mkdir -p "$TESTDIR"
cp -r "$BASE_DIR/busybench" "$TESTDIR"
cd "$TESTDIR/busybench"

retry npm install pify @types/pify typescript gts @types/node >/dev/null
{{if .BinaryHost}}
retry npm install --nodedir="$NODEDIR" --fallback-to-build=false --pprof_binary_host_mirror={{.BinaryHost}} "$PROFILER" >/dev/null
{{else}}
retry npm install --nodedir="$NODEDIR" --build-from-source=pprof "$PROFILER" typescript gts >/dev/null
{{end}}

npm run compile >/dev/null

# Run benchmark, which will collect and save profiles.
node -v
node --trace-warnings build/src/busybench.js {{.DurationSec}}

# Write all output standard out with timestamp.
) 2>&1 | while read line; do echo "$(date): ${line}"; done >&1
`))

type profileSummary struct {
	profileType  string
	functionName string
	sourceFile   string
}

type pprofTestCase struct {
	name         string
	nodeVersion  string
	nvmMirror    string
	wantProfiles []profileSummary
}

func (tc *pprofTestCase) generateScript(tmpl *template.Template) (string, error) {
	var buf bytes.Buffer
	err := tmpl.Execute(&buf,
		struct {
			Name        string
			NodeVersion string
			NVMMirror   string
			DurationSec int
			PprofDir    string
			BinaryHost  string
		}{
			Name:        tc.name,
			NodeVersion: tc.nodeVersion,
			NVMMirror:   tc.nvmMirror,
			DurationSec: 10,
			PprofDir:    *pprofDir,
			BinaryHost:  *binaryHost,
		})
	if err != nil {
		return "", fmt.Errorf("failed to render benchmark script for %s: %v", tc.name, err)
	}
	filename := fmt.Sprintf("%s.sh", tc.name)
	if err := ioutil.WriteFile(filename, buf.Bytes(), os.ModePerm); err != nil {
		return "", fmt.Errorf("failed to write benchmark script for %s to %s: %v", tc.name, filename, err)
	}
	return filename, nil
}

func TestAgentIntegration(t *testing.T) {
	wantProfiles := []profileSummary{
		{profileType: "time", functionName: "busyLoop", sourceFile: "busybench.js"},
		{profileType: "heap", functionName: "benchmark", sourceFile: "busybench.js"},
	}

	testcases := []pprofTestCase{
		{
			name:         fmt.Sprintf("pprof-node6-%s", runID),
			wantProfiles: wantProfiles,
			nodeVersion:  "6",
		},
		{
			name:         fmt.Sprintf("pprof-node8-%s", runID),
			wantProfiles: wantProfiles,
			nodeVersion:  "8",
		},
		{
			name:         fmt.Sprintf("pprof-node10-%s", runID),
			wantProfiles: wantProfiles,
			nodeVersion:  "10",
		},
		{
			name:         fmt.Sprintf("pprof-node11-%s", runID),
			wantProfiles: wantProfiles,
			nodeVersion:  "11",
		},
	}
	if *runOnlyV8CanaryTest {
		testcases = []pprofTestCase{{
			name:         fmt.Sprintf("pprof-v8-canary-%s", runID),
			wantProfiles: wantProfiles,
			nodeVersion:  "node", // install latest version of node
			nvmMirror:    "https://nodejs.org/download/v8-canary",
		}}
	}

	// Prevent test cases from running in parallel.
	runtime.GOMAXPROCS(1)

	for _, tc := range testcases {
		tc := tc // capture range variable
		t.Run(tc.name, func(t *testing.T) {
			bench, err := tc.generateScript(tmpl)
			if err != nil {
				t.Fatalf("failed to initialize bench script: %v", err)
			}

			cmd := exec.Command("/bin/bash", bench)
			var testOut bytes.Buffer
			cmd.Stdout = &testOut
			err = cmd.Run()
			t.Log(testOut.String())
			if err != nil {
				t.Fatalf("failed to execute benchmark: %v", err)
			}

			for _, wantProfile := range tc.wantProfiles {
				profilePath := fmt.Sprintf("%s/busybench/%s.pb.gz", tc.name, wantProfile.profileType)
				if err := checkProfile(profilePath, wantProfile); err != nil {
					t.Errorf("failed to collect expected %s profile: %v", wantProfile.profileType, err)
				}
			}
		})
	}
}

func checkProfile(path string, want profileSummary) error {
	f, err := os.Open(path)
	if err != nil {
		return fmt.Errorf("failed to open profile: %v", err)
	}

	pr, err := profile.Parse(f)
	if err != nil {
		return fmt.Errorf("failed to parse profile: %v", err)
	}

	for _, loc := range pr.Location {
		for _, line := range loc.Line {
			if want.functionName == line.Function.Name && strings.HasSuffix(line.Function.Filename, want.sourceFile) {
				return nil
			}
		}
	}
	return fmt.Errorf("Location (function: %s, file: %s) not found in profiles of type %s", want.functionName, want.sourceFile, want.profileType)
}
