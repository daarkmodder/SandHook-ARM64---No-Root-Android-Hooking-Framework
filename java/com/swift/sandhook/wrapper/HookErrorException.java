package com.swift.sandhook.wrapper;

public class HookErrorException extends Exception {
    public HookErrorException(String message) {
        super(message);
    }
    public HookErrorException(String message, Throwable cause) {
        super(message, cause);
    }
}