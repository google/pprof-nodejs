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
	"archive/tar"
	"bytes"
	"context"
	"flag"
	"fmt"
	"io"
	"io/ioutil"
	"os"
	"os/exec"
	"path/filepath"
	"runtime"
	"strings"
	"testing"
	"text/template"
	"time"

	"github.com/docker/docker/api/types"
	"github.com/docker/docker/api/types/container"
	"github.com/docker/docker/api/types/mount"
	"github.com/docker/docker/client"
	"github.com/google/pprof/profile"
)

var (
	binaryHost          = flag.String("binary_host", "", "host from which to download precompiled binaries; if no value is specified, binaries will be built from source.")
	runOnlyV8CanaryTest = flag.Bool("run_only_v8_canary_test", false, "if true test will be run only with the v8-canary build, otherwise, no tests will be run with v8 canary build")
	pprofDir            = flag.String("pprof_nodejs_path", "", "path to directory containing pprof-nodejs module")
	runOn               = flag.String("run_on", "local", "environment on which to run system test. Either linux-docker, alpine-docker, or local.")

	runID = strings.Replace(time.Now().Format("2006-01-02-15-04-05.000000-0700"), ".", "-", -1)
)

const alpineImage = "node:10-alpine"
const linuxImage = "node:10"

var dockerTmpl = template.Must(template.New("dockerTemplate").Parse(`
	FROM {{.Image}}

	{{if .BuildBinary}}
		{{if .IsAlpine}}
		RUN apk add --no-cache python curl bash build-base
		{{else}}
		RUN apt-get update
		RUN apt-get install -y curl build-essential
		{{end}}
	{{end}}
	`))

var benchTmpl = template.Must(template.New("benchTemplate").Parse(`
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

# Fail on any error.
set -eo pipefail

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

retry npm install --nodedir="$NODEDIR" {{if .BinaryHost}}--fallback-to-build=false --pprof_binary_host_mirror={{.BinaryHost}}{{end}} >/dev/null

npm run compile
npm pack >/dev/null
VERSION=$(node -e "console.log(require('./package.json').version);")
PROFILER="{{.PprofDir}}/pprof-$VERSION.tgz"

# Create and set up directory for running benchmark.
TESTDIR="{{.PprofDir}}/run-system-test/{{.Name}}"
mkdir -p "$TESTDIR"
cp -r "{{.PprofDir}}/run-system-test/busybench" "$TESTDIR"
cd "$TESTDIR/busybench"

retry npm install pify @types/pify typescript gts @types/node >/dev/null
retry npm install --nodedir="$NODEDIR" {{if .BinaryHost}}--fallback-to-build=false --pprof_binary_host_mirror={{.BinaryHost}}{{end}} "$PROFILER" >/dev/null

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

func (tc *pprofTestCase) generateScript() (string, error) {
	var buf bytes.Buffer
	err := benchTmpl.Execute(&buf,
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

// generateDockerfile creates a dockerfile for running the system test, and
// returns the base image for the dockerfile, and a buffer containing the
// dockerfile
func generateDockerfile(runOn string, buildBinary bool) (string, bytes.Buffer, error) {
	var isAlpine bool
	var image string
	switch runOn {
	case "linux-docker":
		image = linuxImage
	case "alpine-docker":
		image = alpineImage
		isAlpine = true
	default:
		return "", bytes.Buffer{}, fmt.Errorf("unrecognized environment to run system test on: %s", runOn)
	}

	var buf bytes.Buffer
	err := dockerTmpl.Execute(&buf,
		struct {
			Image       string
			BuildBinary bool
			IsAlpine    bool
		}{
			Image:       image,
			BuildBinary: buildBinary,
			IsAlpine:    isAlpine,
		})
	if err != nil {
		return "", bytes.Buffer{}, fmt.Errorf("failed to render docker file: %v", err)
	}
	return image, buf, nil
}

func TestAgentIntegration(t *testing.T) {
	ctx := context.Background()

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

	var cli *client.Client
	imageName := fmt.Sprintf("%s-%s", *runOn, runID)
	if *runOn != "local" {
		var err error
		if cli, err = client.NewClientWithOpts(client.FromEnv); err != nil {
			t.Fatalf("failed to create docker client: %v", err)
		}
		if err := buildDockerImage(ctx, cli, imageName, *runOn, *binaryHost == ""); err != nil {
			t.Fatal(err)
		}

	}

	for _, tc := range testcases {
		tc := tc // capture range variable
		t.Run(tc.name, func(t *testing.T) {
			bench, err := tc.generateScript()
			if err != nil {
				t.Fatalf("failed to initialize bench script: %v", err)
			}

			if cli == nil {
				out, err := runLocally(bench)
				t.Log(out.String())
				if err != nil {
					t.Fatalf("failed to execute benchmark: %v", err)
				}
			} else {
				out, err := runOnDocker(ctx, cli, imageName, bench)
				t.Log(out.String())
				if err != nil {
					t.Fatalf("failed to execute benchmark: %v", err)
				}
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

// runLocally executes the benchScript with bash.
func runLocally(benchScript string) (bytes.Buffer, error) {
	cmd := exec.Command("/bin/bash", benchScript)
	var out bytes.Buffer
	cmd.Stdout = &out
	err := cmd.Run()
	return out, err
}

// buildDockerImage creates a docker image running on specified OS (determined
// by runOn), with necessary dependencies for building binaries if buildBinary
// indicates binaries will be built on the docker image.
func buildDockerImage(ctx context.Context, cli *client.Client, imageName, runOn string, buildBinary bool) error {
	baseImage, dockerfile, err := generateDockerfile(runOn, buildBinary)
	if err != nil {
		return fmt.Errorf("failed to generate docker file: %v", err)
	}

	dbCtx, err := dockerBuildContext(dockerfile)
	if err != nil {
		return fmt.Errorf("failed to get docker build context: %v", err)
	}

	_, err = cli.ImagePull(ctx, baseImage, types.ImagePullOptions{})
	if err != nil {
		return fmt.Errorf("failed to pull base docker image %s: %v", baseImage, err)
	}

	_, err = cli.ImageBuild(ctx, dbCtx, types.ImageBuildOptions{
		Tags: []string{imageName},
	})
	if err != nil {
		return fmt.Errorf("failed to build docker image: %v", err)
	}
	return nil
}

// dockerBuildContext takes the text of a dockerfile and returns an io reader
// with a tar archive containing the dockerfile.
func dockerBuildContext(dockerfile bytes.Buffer) (io.Reader, error) {
	var buf bytes.Buffer
	w := tar.NewWriter(&buf)
	defer w.Close()

	if err := w.WriteHeader(&tar.Header{Name: "Dockerfile", Size: int64(dockerfile.Len())}); err != nil {
		return nil, fmt.Errorf("failed to write tar header: %v", err)
	}
	if _, err := w.Write(dockerfile.Bytes()); err != nil {
		return nil, fmt.Errorf("failed to write dockerfile to tar: %v", err)
	}

	return bytes.NewReader(buf.Bytes()), nil
}

// runOnDocker runs the benchScript on the specified docker image.
func runOnDocker(ctx context.Context, cli *client.Client, imageName, benchScript string) (bytes.Buffer, error) {
	benchScript, err := filepath.Abs(benchScript)
	if err != nil {
		return bytes.Buffer{}, fmt.Errorf("failed to get absolute path of %s: %v", benchScript, err)
	}

	resp, err := cli.ContainerCreate(ctx,
		&container.Config{
			Image:   imageName,
			Cmd:     []string{"/bin/bash", benchScript},
			Tty:     true,
			Volumes: map[string]struct{}{fmt.Sprintf("%q:%q", *pprofDir, *pprofDir): {}},
		},
		&container.HostConfig{
			Mounts: []mount.Mount{
				{
					Type:   mount.TypeBind,
					Source: *pprofDir,
					Target: *pprofDir,
				},
			},
		}, nil, "")
	if err != nil {
		return bytes.Buffer{}, fmt.Errorf("failed to created docker container: %v", err)
	}

	if err := cli.ContainerStart(ctx, resp.ID, types.ContainerStartOptions{}); err != nil {
		return bytes.Buffer{}, fmt.Errorf("failed to start container: %v", err)
	}

	statusCh, errCh := cli.ContainerWait(ctx, resp.ID, container.WaitConditionNotRunning)
	select {
	case err := <-errCh:
		if err != nil {
			return bytes.Buffer{}, fmt.Errorf("failed to wait for container: %v", err)
		}
	case <-statusCh:
	}

	out, err := cli.ContainerLogs(ctx, resp.ID, types.ContainerLogsOptions{ShowStdout: true})
	if err != nil {
		return bytes.Buffer{}, fmt.Errorf("failed to get container logs: %v", err)
	}
	var buf bytes.Buffer
	if _, err := buf.ReadFrom(out); err != nil {
		return bytes.Buffer{}, fmt.Errorf("failed to read containder logs: %v", err)
	}
	return buf, nil
}

// checkProfile opens the profile at path and confirms that profile contains
// necessary
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
