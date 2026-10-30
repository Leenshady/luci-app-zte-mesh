// Runs the LuCI view of openwrt/luci-app-zte-mesh against a minimal mock of LuCI's form API and checks that the form offers
// every UCI option the init script reads (tests/test_init.sh "all-options" case, the slot and ACL sections).
// usage: node tests/test_luci.js [zte-mesh.js] [test_init.sh]
const fs = require('fs'), path = require('path');
const viewFile = process.argv[2] || path.join(__dirname, '../openwrt/luci-app-zte-mesh/htdocs/luci-static/resources/view/network/zte-mesh.js');
const initTest = process.argv[3] || path.join(__dirname, 'test_init.sh');
String.prototype.format = function() { let i = 0, a = arguments; return this.replace(/%[sd]/g, () => a[i++]); };
const registered = {}; // section type -> {option: {datatype, depends, ...}}
let uciRole = 'master'; // what uci.get('zte_mesh', 'main', 'mesh_role') answers
class Opt { constructor(n, c) { this.name = n; this.cls = c; this.deps = []; this.values = []; }
	depends(k, v) { this.deps.push([k, v]); } value(v) { this.values.push(v); } }
class Sec { constructor(type) { this.type = type; registered[type] = registered[type] || {}; }
	tab() {} taboption(t, c, n) { return registered[this.type][n] = new Opt(n, c.name); }
	option(c, n) { return registered[this.type][n] = new Opt(n, c.name); } }
class Map { constructor() {} section(c, name, type) { return new Sec(c === GridSection ? name : type); }
	render() { return Promise.resolve({}); } }
class NamedSection {} class GridSection {}
class Flag {} class Value {} class ListValue {} class MultiValue {}
// The view derives ReportingGridSection from form.GridSection. Here the derived class is the same one, which keeps
// the mock's m.section(Class, NAME, ...) keyed on NAME (the section type the init script and this test agree on).
GridSection.extend = () => GridSection;
let viewDef;
const mocks = {
	view: { extend: d => (viewDef = d) },
	dom: { content() {} }, poll: { add() {} }, ui: { createHandlerFn: (t, f) => f, addNotification() {} },
	uci: { load: () => Promise.resolve(), sections: (c, t) => (t === 'zte_mesh_node' ? [{ mac: 'AA:BB:CC:DD:EE:01', name: 'Living room' }] : []) }, network: { getHostHints: () => Promise.resolve({}) },
	rpc: { declare: () => () => Promise.resolve({}) },
	form: { Map, NamedSection, GridSection, Flag, Value, ListValue, MultiValue },
};
// GridSection options are keyed by the section TYPE argument of m.section(form.GridSection, TYPE, ...)
const src = fs.readFileSync(viewFile, 'utf8').replace(/^'use strict';/, '').replace(/^'require [^']*';$/gm, '');
mocks.uci.get = (pkg, sec, opt) => (pkg === 'zte_mesh' && sec === 'main' && opt === 'mesh_role' ? uciRole : null);
const names = Object.keys(mocks);
const fn = new Function(...names, '_', 'E', src);
const E = (t, a, c) => ({ t, a, c, kids: [], appendChild(x) { this.kids.push(x); return x; } });
const result = fn(...names.map(n => mocks[n]), s => s, E);
(async () => {
	const data = [{ installed: true, running: true, state: { running: true, nodes: [] }, age: 1 }, {}, undefined];
	await viewDef.render(data);
	await viewDef.load();
	// node table: ZTE routers that are online get the Restart / Light buttons, others a dash
	{
		const table = viewDef.renderNodes({ state: { nodes: [
			{ mac: 'aa:bb:cc:dd:ee:01', name: 'child', model: 'M', software: 'v', zte: true, online: true, light: 1, stations: 3 },
			{ mac: 'aa:bb:cc:dd:ee:02', name: 'std', model: '', software: '', zte: false, online: true },
			{ mac: 'aa:bb:cc:dd:ee:03', name: 'off', model: '', software: '', zte: true, online: false } ] } }, {});
		const rows = table.kids;
		const buttons = r => JSON.stringify(r).split('"t":"button"').length - 1;
		const text = r => JSON.stringify(r);
		if (!text(rows[0]).includes('Living room') || text(rows[1]).includes('Living room') || !text(rows[0]).includes('"On"') || !text(rows[0]).includes('"3"')) {
			console.log('alias / light / clients cells wrong:', text(rows[0]).slice(0, 400));
			process.exit(1);
		}
		if (rows.length !== 3 || buttons(rows[0]) !== 3 || buttons(rows[1]) !== 0 || buttons(rows[2]) !== 0) {
			console.log('node action buttons wrong:', rows.map(buttons));
			process.exit(1);
		}
	}
	const text = fs.readFileSync(initTest, 'utf8');
	const wanted = { zte_mesh: new Set(['enabled', 'bridge']), /* read by start_service, not by write_config */ zte_mesh_bss: new Set(['band', 'slot', 'enabled', 'ssid', 'key', 'hidden', 'security', 'acl_policy', 'access_minutes', 'force_wifi5']),
	                 zte_mesh_acl: new Set(['mac', 'name', 'band', 'bss']), zte_mesh_timer: new Set(['start', 'end', 'days', 'scope']), zte_mesh_node: new Set(['mac', 'name']) };
	const block = text.slice(text.indexOf('# 8. every optional main option'), text.indexOf('run all-options'));
	for (const m of block.matchAll(/^\toption (\w+) '/gm)) wanted.zte_mesh.add(m[1]);
	// 'mode' is the legacy option of older files: the init script accepts and ignores it, so the form must not offer it.
	wanted.zte_mesh.delete('mode');
	for (const m of text.matchAll(/option (wifi_5g_ssid|wifi_5g_key|wifi_5g_suffix|wifi_dual_band_sync|wifi_24g_ssid|wifi_24g_key|wifi_5g_enabled|wifi_24g_enabled|enabled|bridge) /g)) wanted.zte_mesh.add(m[1]);
	let bad = 0;
	for (const [type, set] of Object.entries(wanted)) {
		for (const name of set) {
			if (!registered[type] || !registered[type][name]) { console.log('LuCI form lacks', type, name); bad++; }
		}
	}
	const extra = Object.keys(registered.zte_mesh || {}).filter(n => !wanted.zte_mesh.has(n));
	if (extra.length) { console.log('form options the init test does not exercise:', extra.join(' ')); bad++; }
	// Child role: the settings this router would push to its own child routers do not apply there, so they must not be
	// offered at all, and the backhaul choice only exists on that side.
	const counts = { main: Object.keys(registered.zte_mesh || {}).length,
		bss: Object.keys(registered.zte_mesh_bss || {}).length,
		acl: Object.keys(registered.zte_mesh_acl || {}).length, child: '' };
	for (const k of Object.keys(registered)) delete registered[k];
	uciRole = 'slave';
	await viewDef.render([{ installed: true, running: true, state: { running: true, nodes: [] }, age: 1 }, {}]);
	{
		const m = registered.zte_mesh || {};
		const forbidden = Object.keys(m).filter(n => n !== 'enabled' && n !== 'bridge' && n !== 'mesh_role' && n !== 'backhaul');
		if (forbidden.length) { console.log('child form still offers controller settings:', forbidden.join(' ')); bad++; }
		if (!m.backhaul) { console.log('child form lacks the backhaul choice'); bad++; }
			if (registered.zte_mesh_bss || registered.zte_mesh_acl || registered.zte_mesh_timer) { console.log('child form still offers the controller grids'); bad++; }
		counts.child = Object.keys(registered.zte_mesh || {}).length + ' options';
	}
	console.log(bad ? 'luci test: FAIL' : 'luci test: ok (' + counts.main + ' main options, ' + counts.bss + ' slot options, ' + counts.acl + ' ACL options, child form ' + counts.child + ')');
	process.exit(bad ? 1 : 0);
})().catch(e => { console.log('luci test: exception', e.stack); process.exit(1); });
