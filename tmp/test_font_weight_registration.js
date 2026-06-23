const fs = require('fs');
const path = require('path');
const CREngine = require('../tools/epub-to-xtc-converter/web/crengine.js');

const fontPath = path.join(__dirname, '../tools/epub-to-xtc-converter/fonts/CorpSrcWinSong-.4.ttf');

CREngine().then(function(Module) {
    const renderer = new Module.EpubRenderer(480, 800);
    const data = fs.readFileSync(fontPath);
    const bytes = new Uint8Array(data);
    const ptr = Module.allocateMemory(bytes.length);

    Module.HEAPU8.set(bytes, ptr);

    console.log('before', renderer.getAvailableFonts());
    console.log('registerFontFromMemory', renderer.registerFontFromMemory(ptr, bytes.length, 'CorpSrcWinSong-.4.ttf'));
    console.log('after regular', renderer.getAvailableFonts());

    try {
        console.log(
            'registerFontAsWeight',
            renderer.registerFontAsWeight(ptr, bytes.length, 'CorpSrcWinSong-.4.ttf', true, false)
        );
    } catch (err) {
        console.log('registerFontAsWeight ERR', err && err.message ? err.message : String(err));
    }

    console.log('after weight', renderer.getAvailableFonts());
    Module.freeMemory(ptr);
}).catch(function(err) {
    console.error(err);
    process.exit(1);
});
