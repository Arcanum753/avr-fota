function hexFileHeadCheck(contents) {
    var signture = '';
    var project = '';
    // var version = '';
    // var buildtime = '';
    // var builddate = '';
    var _c;
    let _isParam = false;
    let _isValueSign = false; 
    let _isValueProj = false; 
    // let _isValueVers = false; 
    // let _isValueDate = false; 
    // let _isValueTime = false; 
    let _strParam = '';
    for (let _index = 0; _index < contents.length; _index++) {
        _c = contents.substr(_index, 1) ;
        if (_c == '\n' || _c == '\r' || _c == '\t') { 
            if (_isValueSign == true) {_isValueSign = false;    }
            if (_isValueProj == true) {_isValueProj = false;    }
            // if (_isValueVers == true) {_isValueVers = false;    }
            // if (_isValueDate == true) {_isValueDate = false;    }
            // if (_isValueTime == true) {_isValueTime = false;    }
        }
        if (_isValueSign == true) { signture      += _c;  }
        if (_isValueProj == true) { project  += _c;  }
        // if (_isValueVers == true) { version       += _c;  }
        // if (_isValueDate == true) { builddate     += _c;  }
        // if (_isValueTime == true) { buildtime     += _c;  }

        if (_strParam == 'sign'   ) { _isValueSign = true;}
        if (_strParam == 'proj'   ) { _isValueProj = true;}
        // if (_strParam == 'vers'   ) { _isValueVers = true;}
        // if (_strParam == 'date'   ) { _isValueDate = true;}
        // if (_strParam == 'time'   ) { _isValueTime = true;}

        if (_isParam == true && _c !== '$') { _strParam += _c; }
        if (_isParam == true && _c == ' ') { _isParam = false; _strParam = ''; }
        if (_c == '$')             { _isParam = true;}
    }
    var ret = '';
    ret += 'sign|' + signture       +"|div\n";
    ret += 'proj|' + project        +"|div\n";
    // ret += 'vers|' + version        +"|div\n";
    // ret += 'date|' + builddate      +"|div\n";
    // ret += 'time|' + buildtime      +"|div\n";
    console.log(ret);
    return ret;
}
