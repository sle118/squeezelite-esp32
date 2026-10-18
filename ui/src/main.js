import { mount } from 'svelte';
import App from './App.svelte';

// The device serves a small "loading" placeholder inside #app; drop it as soon
// as the app takes over (Svelte 5 appends, it does not replace the children).
document.getElementById('boot')?.remove();

const app = mount(App, { target: document.getElementById('app') });

export default app;
