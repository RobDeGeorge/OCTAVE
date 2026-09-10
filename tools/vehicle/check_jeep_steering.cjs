// Run with node tools/vehicle/check_jeep_steering.cjs [optional poses.json path].
const fs = require('node:fs');
const path = require('node:path');
const vm = require('node:vm');
const assert = require('node:assert/strict');
const root = path.resolve(__dirname, '../..');
const context = {};
vm.createContext(context);
vm.runInContext(fs.readFileSync(path.join(root, 'frontend/JeepSteering.js'), 'utf8').replace('.pragma library', ''), context);
const geometry = JSON.parse(fs.readFileSync(path.join(__dirname, 'steering_geometry.json')));
const neutral = context.solve(0, geometry);
const knots = context.tiePoints(neutral, geometry);
const lengths = knots.slice(1).map((v, i) => context.length(context.sub(v, knots[i])));
const poses = [];
let previous = [-Infinity, -Infinity];
for (let i = -100; i <= 100; ++i) {
    const input = i / 100;
    const pose = context.solve(input, geometry);
    assert(Math.abs(pose.dragError) < 1e-8 && Math.abs(pose.tieError) < 1e-8, 'Rod length drift');
    const points = context.tiePoints(pose, geometry);
    points.slice(1).forEach((v, j) => assert(Math.abs(context.length(context.sub(v, points[j])) - lengths[j]) < 1e-8));
    for (const [j, angle] of [pose.driverAngle, pose.passengerAngle].entries()) {
        assert(Number.isFinite(angle) && Math.abs(angle) < 35);
        assert(angle >= previous[j] - 1e-8, 'Steering must be monotonic');
        previous[j] = angle;
    }
    if (input > .1) assert(pose.driverAngle > pose.passengerAngle, 'Inside left wheel must turn farther');
    if (input < -.1) assert(pose.passengerAngle < pose.driverAngle, 'Inside right wheel must turn farther');
    if (i % 10 === 0) poses.push({input, ...pose, tiePoints: points});
}
assert(Math.abs(neutral.driverAngle) < 1e-9 && Math.abs(neutral.passengerAngle) < 1e-9);
assert.equal(context.solve(2, geometry).driverAngle, context.solve(1, geometry).driverAngle);
assert.equal(context.solve(-2, geometry).driverAngle, context.solve(-1, geometry).driverAngle);
if (process.argv[2]) fs.writeFileSync(process.argv[2], JSON.stringify(poses));
console.log('PASS: 201 steering positions; constant rod lengths; monotonic, bounded turns; inside-wheel angle and input clamping');
