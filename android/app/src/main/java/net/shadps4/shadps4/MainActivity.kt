package net.shadps4.shadps4

import android.app.Activity
import android.os.Bundle
import android.widget.Button
import android.widget.EditText
import android.widget.LinearLayout
import android.widget.Toast

class MainActivity : Activity() {
    external fun runGame(path: String): Int

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        val layout = LinearLayout(this)
        layout.orientation = LinearLayout.VERTICAL
        val input = EditText(this)
        input.hint = "Game path"
        val button = Button(this)
        button.text = "Run Game"
        button.setOnClickListener {
            val result = runGame(input.text.toString())
            Toast.makeText(this, "Result $result", Toast.LENGTH_LONG).show()
        }
        layout.addView(input)
        layout.addView(button)
        setContentView(layout)
    }

    companion object {
        init {
            System.loadLibrary("shadps4")
        }
    }
}
