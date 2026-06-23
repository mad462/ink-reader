const fs = require('fs');
const CREngine = require('../tools/epub-to-xtc-converter/web/crengine.js');

const epubPath = 'D:\\Desktop\\电子书相关\\哈代作品集..epub';

CREngine().then(function(Module) {
    const renderer = new Module.EpubRenderer(480, 800);
    const data = fs.readFileSync(epubPath);
    const bytes = new Uint8Array(data);
    const ptr = Module.allocateMemory(bytes.length);

    Module.HEAPU8.set(bytes, ptr);
    renderer.loadEpubFromMemory(ptr, bytes.length);

    console.log('initial page', renderer.getCurrentPage());
    const toc = renderer.getToc();
    console.log('toc length', toc.length);

    function tryCall() {
        const cases = [
            [0],
            [1],
            [2],
            [3],
            [4],
            [5],
            [6],
            [7],
            [8],
            [9],
            [10],
            [11],
            [12],
            [13],
            [14],
            [15],
            [16],
            [17],
            [18],
            [19],
            [20],
            [21],
            [22],
            [23],
            [24],
            [25],
            [26],
            [27],
            [28],
            [29],
            [30]
        ];

        for (const args of cases) {
            try {
                const result = renderer.goToTocItem.apply(renderer, args);
                console.log('goToTocItem', JSON.stringify(args), '=>', result, 'page', renderer.getCurrentPage());
            } catch (err) {
                console.log('goToTocItem', JSON.stringify(args), 'ERR', err && err.message ? err.message : String(err));
            }
        }
    }

    tryCall();
    Module.freeMemory(ptr);
}).catch(function(err) {
    console.error(err);
    process.exit(1);
});
