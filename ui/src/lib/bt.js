// A2DP / media state names, mirroring components/driver_bt/bt_app_source.c
// (APP_AV_STATE_* and APP_AV_MEDIA_STATE_*).

const A2D = [
  { name: 'Idle', tone: 'idle' },
  { name: 'Discovering', tone: 'busy' },
  { name: 'Discovered', tone: 'busy' },
  { name: 'Unconnected', tone: 'off' },
  { name: 'Connecting', tone: 'busy' },
  { name: 'Connected', tone: 'on' },
  { name: 'Disconnecting', tone: 'busy' },
];

const MEDIA = [
  'idle', // APP_AV_MEDIA_STATE_IDLE
  'starting', // APP_AV_MEDIA_STATE_STARTING
  'playing', // APP_AV_MEDIA_STATE_STARTED
  'stopping', // APP_AV_MEDIA_STATE_STOPPING
  'wait-disconnect', // APP_AV_MEDIA_STATE_WAIT_DISCONNECT
];

export function describeBt(status, sub) {
  const a2d = A2D[status];
  if (!a2d) {
    return { label: 'неизвестно', tone: 'off', media: null, a2dName: null, mediaName: MEDIA[sub] ?? null };
  }
  const mediaName = MEDIA[sub] ?? null;
  return {
    label: a2d.name,
    tone: a2d.tone,
    // the media state only means something while the link is up
    media: a2d.name === 'Connected' ? mediaName : null,
    a2dName: a2d.name,
    mediaName,
  };
}
