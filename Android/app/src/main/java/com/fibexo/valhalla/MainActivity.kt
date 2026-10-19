package com.fibexo.valhalla

import org.libsdl.app.SDLActivity

class MainActivity :  SDLActivity() {
    override fun getLibraries(): Array<out String> {
        return arrayOf("Valhalla")
    }
}