import fs from 'fs';

if (fs.existsSync(process.argv[1])) {
  fs.writeFileSync('oom_check.log', 'ok');
} else {
  fs.writeFileSync('oom_check.log', 'ko');
}
