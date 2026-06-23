const fs = require('fs');
const path = require('path');
const CREngine = require('../tools/epub-to-xtc-converter/web/crengine.js');

const epubPath = 'D:\\Desktop\\电子书相关\\哈代作品集..epub';

CREngine().then(function(Module) {
    const renderer = new Module.EpubRenderer(480, 800);
    const data = fs.readFileSync(epubPath);
    const bytes = new Uint8Array(data);
    const ptr = Module.allocateMemory(bytes.length);

    Module.HEAPU8.set(bytes, ptr);
    renderer.loadEpubFromMemory(ptr, bytes.length);

    const toc = renderer.getToc();
    console.log(JSON.stringify(toc.slice(0, 80), null, 2));

    Module.freeMemory(ptr);
}).catch(function(err) {
    console.error(err);
    process.exit(1);
});
