import { mount } from 'svelte'
import './app.css'
import App from './App.svelte'
import { octave } from './lib/octave'

octave.connect()
const app = mount(App, { target: document.getElementById('app')! })
export default app
