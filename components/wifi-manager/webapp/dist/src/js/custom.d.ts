declare global {
    interface Window {
        hideSurrounding: (obj: HTMLElement) => void;
        hFlash: () => void;
        handleReboot: (link: string) => void;
        setURL: (button: HTMLButtonElement) => void;
        runCommand: (button: HTMLButtonElement, reboot: boolean) => void;
    }
    interface String {
        format(...args: any[]): string;
        encodeHTML(): string;
    }
    interface Date {
        toLocalShort(): string;
    }
}
export {};
