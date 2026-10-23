// Dev page: the video player fed from dev/test.h264 by a fake source that
// plays access units at 30 fps and loops. Buttons stall the feed (so the
// player falls behind and must drop to a keyframe) and reset it. Touches
// are logged, never sent anywhere. Make the file with dev/make_test_video.sh.
import { mount } from 'svelte'
import '../app.css'
import VideoDev from './VideoDev.svelte'

mount(VideoDev, { target: document.getElementById('app')! })
